param($Threads = $null)

$AdditionalObjects = @()
mkdir obj 2>$null | Out-Null

function Format-TimeSpan {
    param([TimeSpan]$ts)
    if ($ts.TotalSeconds -lt 1) { $seconds = "{0:0.0}" -f [math]::Round($ts.TotalSeconds, 3); return "$seconds s" }
    if ($ts.TotalSeconds -lt 4) { $seconds = "{0:0.0}" -f [math]::Round($ts.TotalSeconds, 2); return "$seconds s" }
    if ($ts.TotalSeconds -lt 10) { $seconds = "{0:0.0}" -f [math]::Round($ts.TotalSeconds, 1); return "$seconds s" }
    $pts = [System.Collections.Generic.List[string]]::new()
    $hours = [math]::Floor($ts.TotalHours)
    if ($hours -gt 0) { $pts.Add("${hours} h") }
    if ($ts.Minutes -gt 0) { $pts.Add("$($ts.Minutes) min") }
    if ($ts.Seconds -gt 0) { $pts.Add("$($ts.Seconds) s") }
    if ($pts.Count -eq 0) { return "0 s" }
    return $pts -join " "
}

# ----------------------- OTHER FILES BUILD ---------------------

$s = "$PSScriptRoot/scheduler/fibers.s"
$d = "$PSScriptRoot/obj/fibers.s.o"
if ((gi $s 2>$null | % LastWriteTime) -gt (gi $d 2>$null | % LastWriteTime)) {
    $start = Get-Date
    nasm -f win64 $s -o $d
    $t = Format-TimeSpan ((Get-Date) - $start)
    Write-Host "Built scheduler/fibers.s in $t" -Fore green
} else {
    Write-Host "skip  scheduler/fibers.s" -Fore yellow
}
$AdditionalObjects += "$PSScriptRoot/obj/fibers.s.o"

# ----------------------- C FILES BUILD ---------------------

$FLAGS = @(
    "-g"
    "-std=gnu2y"
    # "-fsanitize=address"
    "-I./utilities"
    "-I./api"
    "-I./scheduler"
    "-I./operations"
    "-I./external"
    "-D_CRT_SECURE_NO_WARNINGS"
    
    "-Wall"
    "-Watomic-implicit-seq-cst"
)

$LINK_FLAGS = @(
    "-g"
)

$LIBS = @(
    "-luv"
    "-Lexternal"
)

function Build {
    $f = ls -r "$PSScriptRoot/*.c" | ? {$_.FullName -notmatch "[/\\](old|test)[/\\]"}
    $thd = $Threads ? @{ThrottleLimit=$Threads} : @{}
    $b = $f | % @thd -parallel {$n = rvpa $_ -RelativeBasePath $using:PSScriptRoot -Relative | % { $_ -replace "^\.(/|\\)" }
        $dst = "$using:PSScriptRoot/obj/$n.o"
        $dep = "$using:PSScriptRoot/obj/$n.d"
        $needBuild = $true
        if ((Test-Path $dst) -and (Test-Path $dep)) {
            $dstTime = (gi $dst).LastWriteTime
            $rawDeps = (gc $dep -Raw) -replace "\\\r?\n", " " -replace "^.+:\s+"
            $deps = [regex]::Split($rawDeps.Trim(), "(?<!\\)\s+") | % { $_ -replace "\\ ", " " } | ? { $_ }
            $isUpToDate = $true
            foreach ($d in (@($n) + $deps)) {
                if (!(Test-Path $d) -or ((gi $d).LastWriteTime -gt $dstTime)) {
                    $isUpToDate = $false
                    break
                }
            }
            $needBuild = !$isUpToDate
        }
        if (!$needBuild) {
            Write-Host "skip  $n -> obj/$n.o" -Fore yellow
            return [pscustomobject]@{
                Source    = $n ?? "<unknown>"
                Name      = $dst
                Status    = $true
                Errors    = 0
                Warnings  = 0
                ErrorText = ""
                Skipped   = $true
            }
        }
        Write-Host "build $n -> obj/$n.o" -Fore yellow
        if (!(Test-Path (Split-Path $dst))) { mkdir -Force (Split-Path $dst) | Out-Null }
        $ansi = ($IsWindows ? @("-fansi-escape-codes") : @())
        clang @using:FLAGS -MMD -MF $dep -c $n -o $dst -fcolor-diagnostics @ansi 2>variable:err 1>variable:res
        if ($res | ? { ${_}?.Trim() }) { $res | % { Write-Host $_ } }
        $nocolor = $err -replace "\x1B\[[0-9;]*[a-zA-Z]"
        [pscustomobject]@{
            Source    = $n ?? "<unknown>"
            Name      = $dst
            Status    = !$LASTEXITCODE
            Errors    = $nocolor | sls ": error:" -AllMatches | % Matches | measure | % Count
            Warnings  = $nocolor | sls ": warning:" -AllMatches | % Matches | measure | % Count
            ErrorText = $err
            Skipped   = $false
        }
    }
    $bad = $b | ? -Not Status
    $es = $bad | % Errors | measure -Sum | % Sum
    $ws = $bad | % Warnings | measure -Sum | % Sum

    if ($bad) {
        Write-Host "`nError details:`n`n"
        $bad | % {
            $n = rvpa $_.Source -RelativeBasePath $PSScriptRoot -Relative | % {$_ -replace "^\.(/|\\)"}
            Write-Host "---- in file '$n' $($_.Errors) errors, $($_.Warnings) warnings:" -Fore red
            Write-Host ($_.ErrorText -join "`n")
            Write-Host "`n"
        }
    }

    Write-Host "----"
    if ($bad) {
        Write-Host "Build failed:" -Fore red
    } else {
        Write-Host "Files resolved:"
    }
    Write-Host "Total:      $($f.Count) files [$($b.Count) build results]"
    if ($bad) { Write-Host "Failed:     $($bad.Count) files" -Fore red }
    Write-Host "Succeeded:  $($b.Count - $bad.Count) files" -Fore green
    Write-Host "Errors:     $es" -Fore red
    Write-Host "Warnings:   $ws" -Fore yellow

    if ($bad) {
        Write-Host "----"
        Write-Host "Failed files: [see error details above]"
        $bad | % {
            $n = rvpa $_.Source -RelativeBasePath $PSScriptRoot -Relative | % {$_ -replace "^\.(/|\\)"}
            Write-Host "* $n [$($_.Errors) errors, $($_.Warnings) warnings]"
        }
    } else {
        $deps = @($AdditionalObjects) + @($b | % Name)
        if (($deps | gi 2>$null | % LastWriteTime) -gt (gi "$PSScriptRoot/a.exe" 2>$null | % LastWriteTime)) {
            Write-Host "Linking" -Fore yellow
            $ansi = ($IsWindows ? @("-fansi-escape-codes") : @())
            clang -fcolor-diagnostics @LINK_FLAGS @ansi @($b | % Name) @AdditionalObjects -o "$PSScriptRoot/a.exe" @LIBS | oh
            if ($LASTEXITCODE) {
                Write-Host "Build failed:" -Fore red
                Write-Host "Linkage failed." -Fore red
                return $false
            }
        } else {
            Write-Host "Linking skipped" -Fore yellow
        }
    }

    if (!$bad) {
        Write-Host "Build succseeded" -Fore green
    }

    return !$bad
}


$t1 = Get-Date
$res = Build
$t2 = Get-Date
$t = Format-TimeSpan ($t2 - $t1)
if (!$res) {
    Write-Host "`n`n->`nBuild failed in $t." -Fore red
} else {
    Write-Host "`n`n->`nBuild succeeded in $t." -Fore green
    Write-Host "see result at a.exe" -Fore green
}
