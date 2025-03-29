# Copyright (c) 2025 Roger Brown.
# Licensed under the MIT License.

param(
	$CertificateThumbprint = '601A8B683F791E51F647D34AD102C38DA4DDB65F'
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

trap
{
	throw $PSItem
}

$Version = '1.0.0'

Write-Output "Version is $Version"

if ($IsWindows -or ( 'Desktop' -eq $PSEdition ))
{
	foreach ($EDITION in 'Community', 'Professional')
	{
		$VCVARSDIR = "${Env:ProgramFiles}\Microsoft Visual Studio\2022\$EDITION\VC\Auxiliary\Build"

		if ( Test-Path -LiteralPath $VCVARSDIR -PathType Container )
		{
			break
		}
	}

	$VCVARSARM = 'vcvarsarm.bat'
	$VCVARSARM64 = 'vcvarsarm64.bat'
	$VCVARSAMD64 = 'vcvars64.bat'
	$VCVARSX86 = 'vcvars32.bat'
	$VCVARSHOST = 'vcvars32.bat'

	switch ($Env:PROCESSOR_ARCHITECTURE)
	{
		'AMD64' {
			$VCVARSX86 = 'vcvarsamd64_x86.bat'
			$VCVARSARM = 'vcvarsamd64_arm.bat'
			$VCVARSARM64 = 'vcvarsamd64_arm64.bat'
			$VCVARSHOST = $VCVARSAMD64
			}
		'ARM64' {
			$VCVARSX86 = 'vcvarsarm64_x86.bat'
			$VCVARSARM = 'vcvarsarm64_arm.bat'
			$VCVARSAMD64 = 'vcvarsarm64_amd64.bat'
			$VCVARSHOST = $VCVARSARM64
		}
		'X86' {
			$VCVARSXARM64 = 'vcvarsx86_arm64.bat'
			$VCVARSARM = 'vcvarsx86_arm.bat'
			$VCVARSAMD64 = 'vcvarsx86_amd64.bat'
		}
		Default {
			throw "Unknown architecture $Env:PROCESSOR_ARCHITECTURE"
		}
	}

	$VCVARSARCH = @{'arm' = $VCVARSARM; 'arm64' = $VCVARSARM64; 'x86' = $VCVARSX86; 'x64' = $VCVARSAMD64}

	$ARCHLIST = ( $VCVARSARCH.Keys | ForEach-Object {
		$VCVARS = $VCVARSARCH[$_];
		if ( Test-Path -LiteralPath "$VCVARSDIR/$VCVARS" -PathType Leaf )
		{
			$_
		}
	} | Sort-Object )

	$ARCHLIST | ForEach-Object {
		New-Object PSObject -Property @{
			Architecture=$_;
			Environment=$VCVARSARCH[$_]
		}
	} | Format-Table -Property Architecture,'Environment'

	Push-Location 'win32'

	$Win32Dir = $PWD

	foreach ($DIR in 'obj', 'bin', 'bundle')
	{
		if (Test-Path -LiteralPath $DIR)
		{
			Remove-Item -LiteralPath $DIR -Force -Recurse
		}
	}

	try
	{
		$ARCHLIST | ForEach-Object {
			$ARCH = $_

			$VCVARS = ( '{0}\{1}' -f $VCVARSDIR, $VCVARSARCH[$ARCH] )

			$VersionStr4="$Version.0"
			$VersionInt4=$VersionStr4.Replace(".",",")
			$VersionParts=$Version.Split('.')

			$env:PRODUCTVERSION = $VersionStr4
			$env:PRODUCTARCH = $ARCH
			$env:PRODUCTWIN64 = 'yes'
			$env:PRODUCTPROGFILES = 'ProgramFiles64Folder'
			$env:PRODUCTSYSFILES = 'System64Folder'
			$env:INSTALLERVERSION = '500'
			$env:LINKVERSION = $VersionParts[0]+'.'+$VersionParts[1]

			switch ($ARCH)
			{
				'arm64' {
					$env:UPGRADECODE = '8E0CF550-D547-42CB-802B-9C842FE6D513'
				}

				'arm' {
					$env:UPGRADECODE = 'CD5C37CA-07F6-4047-8DF9-F3016BE02EE5'
					$env:PRODUCTWIN64 = 'no'
					$env:PRODUCTPROGFILES = 'ProgramFilesFolder'
					$env:PRODUCTSYSFILES = 'SystemFolder'
				}

				'x86' {
					$env:UPGRADECODE = '966F2B4E-FB78-4F2D-8D2B-BBCD2EA2C936'
					$env:PRODUCTWIN64 = 'no'
					$env:PRODUCTPROGFILES = 'ProgramFilesFolder'
					$env:PRODUCTSYSFILES = 'SystemFolder'
					$env:INSTALLERVERSION = '200'
				}

				'x64' {
					$env:UPGRADECODE = '5938E230-77DB-4167-A94A-4880ABB5C34C'
					$env:INSTALLERVERSION = '200'
				}
			}

			@"
CALL "$VCVARS"
IF ERRORLEVEL 1 EXIT %ERRORLEVEL%
NMAKE /NOLOGO clean
IF ERRORLEVEL 1 EXIT %ERRORLEVEL%
NMAKE /NOLOGO DEPVERS_conlog_STR4="$VersionStr4" DEPVERS_conlog_INT4="$VersionInt4" CertificateThumbprint="$CertificateThumbprint" BundleThumbprint="$BundleThumbprint"
EXIT %ERRORLEVEL%
"@ | & "$env:COMSPEC"

			if ($LastExitCode -ne 0)
			{
				exit $LastExitCode
			}
		}

		Push-Location 'bin'

		try
		{
			Compress-Archive $ARCHLIST -DestinationPath "..\conlog-$Version-win.zip" -Force

			$ARCHLIST | ForEach-Object {
				$ARCH = $_
				$VCVARS = ( '{0}\{1}' -f $VCVARSDIR, $VCVARSARCH[$ARCH] )
				foreach ($EXE in "$ARCH\conlog.exe")
				{
					$MACHINE = ( @"
@CALL "$VCVARS" > NUL:
IF ERRORLEVEL 1 EXIT %ERRORLEVEL%
dumpbin /headers $EXE
IF ERRORLEVEL 1 EXIT %ERRORLEVEL%
EXIT %ERRORLEVEL%
"@ | & "$env:COMSPEC" /nologo /Q | Select-String -Pattern " machine " )

					$MACHINE = $MACHINE.ToString().Trim()

					$MACHINE = $MACHINE.Substring($MACHINE.LastIndexOf(' ')+1)

					New-Object PSObject -Property @{
						Architecture=$ARCH;
						Executable=$EXE;
						Machine=$MACHINE;
						FileVersion=(Get-Item $EXE).VersionInfo.FileVersion;
						ProductVersion=(Get-Item $EXE).VersionInfo.ProductVersion;
						FileDescription=(Get-Item $EXE).VersionInfo.FileDescription
					}
				}
			} | Format-Table -Property Architecture, Executable, Machine, FileVersion, ProductVersion, FileDescription
		}
		finally
		{
			Pop-Location
		}
	}
	finally
	{
		Pop-Location
	}
}
