param($Threads = $null)

mkdir obj 2>$null | Out-Null
mkdir build 2>$null | Out-Null

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

function Build-Target {
    param(
        [string]$Output,
        [string[]]$Files,
        [string[]]$CFlags = @(),
        [string[]]$NasmFlags = @(),
        [string[]]$LinkFlags = @(),
        [string[]]$Libs = @(),
        [scriptblock]$PostBuild = $null
    )

    Write-Host "`n=== Building target: $Output ===" -Fore cyan
    $thd = $Threads ? @{ThrottleLimit=$Threads} : @{}
    
    $b = $Files | % @thd -parallel {
        $n = rvpa $_ -RelativeBasePath $using:PSScriptRoot -Relative | % { $_ -replace "^\.(/|\\)" }
        $dst = "$using:PSScriptRoot/obj/$n.o"
        $dep = "$using:PSScriptRoot/obj/$n.d"
        $ext = [System.IO.Path]::GetExtension($n).ToLower()
        
        $needBuild = $true
        if (Test-Path $dst) {
            $dstTime = (gi $dst).LastWriteTime
            if ($ext -eq ".s" -or $ext -eq ".asm") {
                $needBuild = ((gi $n).LastWriteTime -gt $dstTime)
            } elseif (Test-Path $dep) {
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

        if ($ext -eq ".s" -or $ext -eq ".asm") {
            nasm @using:NasmFlags $n -o $dst 2>variable:err 1>variable:res
        } else {
            clang @using:CFlags -MMD -MF $dep -c $n -o $dst -fcolor-diagnostics @ansi 2>variable:err 1>variable:res
        }

        if ($res | ? { ${_}?.Trim() }) { $res | % { Write-Host $_ } }
        $nocolor = $err -replace "\x1B\[[0-9;]*[a-zA-Z]"
        [pscustomobject]@{
            Source    = $n ?? "<unknown>"
            Name      = $dst
            Status    = !$LASTEXITCODE
            Errors    = $nocolor | sls "error:" -AllMatches | % Matches | measure | % Count
            Warnings  = $nocolor | sls "warning:" -AllMatches | % Matches | measure | % Count
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
    Write-Host ($bad ? "Build failed:" : "Files resolved:") -Fore ($bad ? "Red" : "White")
    Write-Host "Total:      $($Files.Count) files [$($b.Count) build results]"
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
        return $false
    }

    $outPath = "$PSScriptRoot/build/$Output"
    $objs = @($b | % Name)
    $outTime = (gi $outPath 2>$null | % LastWriteTime)
    $needLink = !$outTime -or (($objs | gi 2>$null | % LastWriteTime | measure -Maximum).Maximum -gt $outTime)

    if ($needLink) {
        Write-Host "Linking $Output" -Fore yellow
        $ansi = ($IsWindows ? @("-fansi-escape-codes") : @())
        clang -fcolor-diagnostics @LinkFlags @ansi @objs -o $outPath @Libs | oh
        if ($LASTEXITCODE) {
            Write-Host "Linkage of $Output failed." -Fore red
            return $false
        }
    } else {
        Write-Host "Linking $Output skipped" -Fore yellow
    }

    if ($needLink -and $PostBuild) {
        Write-Host "Executing post build steps" -Fore yellow
        $previousPreference = $ErrorActionPreference
        $previousNativePreference = $PSNativeCommandUseErrorActionPreference
        $ErrorActionPreference = 'Stop'
        $PSNativeCommandUseErrorActionPreference = $true
        try {
            & $PostBuild
        } catch {
            Write-Host "Post build steps failed: $($_.Exception.Message)" -Fore yellow
            return $false
        } finally {
            $ErrorActionPreference = $previousPreference
            $PSNativeCommandUseErrorActionPreference = $previousNativePreference
        }
    }

    Write-Host "Target $Output succeeded" -Fore green
    return $true
}

# ----------------------- CONFIGURATION ---------------------

$BASE_FLAGS = @(
    "-g"
    "-std=gnu2y"
    # "-fsanitize=address"
    "-I./utilities"
    "-I./api"
    "-I./scheduler"
    "-I./configuration"
    "-I./modules"
    "-I./network"
    "-I./operations"
    "-I./external"
    "-I./external/msquic/include"
    "-D_CRT_SECURE_NO_WARNINGS"
    "-DQUIC_API_ENABLE_PREVIEW_FEATURES=1"
    "-Wall"
    "-Watomic-implicit-seq-cst"
)

$NASM_FLAGS = $IsWindows ? @("-f", "win64") : @("-f", "elf64")

$CoreOut    = $IsWindows ? "core.dll" : "libcore.so"
$AppOut     = $IsWindows ? "a.exe"    : "a"

$CORE_CFLAGS = $BASE_FLAGS + @("-DCORE_BUILD") + ($IsWindows ? @() : @("-fPIC", "-fvisibility=hidden"))
$CORE_LFLAGS = @("-g", "-shared") + ($IsWindows ? @("-Wl,/IMPLIB:$PSScriptRoot/build/core.lib") : @())
$CORE_LIBS   = @("-lmsquic", "-Lexternal/msquic/bin/x64", "-Lexternal/msquic/lib/x64")
$CORE_POSTBUILD = {
    cp "./external/msquic/bin/x64/msquic.dll" "./build/"
    cp "./external/msquic/bin/x64/msquic.pdb" "./build/"
}

$APP_CFLAGS  = $BASE_FLAGS
$APP_LFLAGS  = @("-g") + ($IsWindows ? @() : @("-Wl,-rpath,`$ORIGIN"))
$APP_LIBS    = $IsWindows ? @("$PSScriptRoot/build/core.lib") : @("-Lbuild", "-lcore")



# ----------------------- EXECUTION ---------------------

$t1 = Get-Date

$allC = ls -r "$PSScriptRoot/*.c" | ? { $_.FullName -notmatch "[/\\](old|test)[/\\]" }
$appFiles  = @("$PSScriptRoot/a.c", "$PSScriptRoot/utilities/mp.c")
$coreFiles = @("$PSScriptRoot/scheduler/fibers.s") + ($allC | ? { $_.FullName -ne (gi "$PSScriptRoot/a.c").FullName } | % FullName)

$res = (Build-Target -Output $CoreOut -Files $coreFiles -CFlags $CORE_CFLAGS -NasmFlags $NASM_FLAGS -LinkFlags $CORE_LFLAGS -Libs $CORE_LIBS -PostBuild $CORE_POSTBUILD) -and
       (Build-Target -Output $AppOut  -Files $appFiles  -CFlags $APP_CFLAGS  -LinkFlags $APP_LFLAGS  -Libs $APP_LIBS)

$t2 = Get-Date
$t = Format-TimeSpan ($t2 - $t1)

if (!$res) {
    Write-Host "`n`n->`nBuild failed in $t." -Fore red
    exit 1
} else {
    Write-Host "`n`n->`nBuild succeeded in $t." -Fore green
    Write-Host "see result at $AppOut and $CoreOut" -Fore green
}

