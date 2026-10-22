param($Threads = $null)

$FLAGS = @(
    "-g"
    "-std=gnu2y"
    # "-fsanitize=address"
    "-I./inc"
    "-I./external"
    "-I./core/api"
)

$LIBS = @(
    "-luv"
    "-Lexternal"
)

function Build {
    $f = ls -r "$PSScriptRoot/*.c"
    $thd = $Threads ? @{ThrottleLimit=$Threads} : @{}
    $b = $f | % @thd -parallel {
        $n = rvpa $_ -RelativeBasePath $using:PSScriptRoot -Relative | % {$_ -replace "^\.(/|\\)"}
        Write-Host "build $n -> obj/$n.o" -Fore yellow
        $dst = "$using:PSScriptRoot/obj/$n.o"
        if (!(Test-Path (Split-Path $dst))) {
            mkdir -Force (Split-Path $dst)
        }
        $ansi = ($IsWindows ? @("-fansi-escape-codes") : @())
        clang @using:FLAGS -c $n -o $dst -fcolor-diagnostics @ansi 2>variable:err 1>variable:res
        if ($res | ? {${_}?.Trim()}) {
            $res | % { Write-Host $_ }
        }
        $nocolor = $err -replace "\x1B\[[0-9;]*[a-zA-Z]"
        [pscustomobject]@{
            Source = $n
            Name = $dst
            Status = !$LASTEXITCODE
            Errors = $nocolor | sls ": error:" -AllMatches | % Matches | measure | % Count
            Warnings = $nocolor | sls ": warning:" -AllMatches | % Matches | measure | % Count
            ErrorText = $err
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
        clang @($b | % Name) -o a.exe @LIBS
        if ($LASTEXITCODE) {
            Write-Host "Build failed:" -Fore red
            Write-Host "Linkage failed." -Fore red
            return $false
        }
    }

    if (!$bad) {
        Write-Host "Build succseeded" -Fore green
    }

    return !$bad
}

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
