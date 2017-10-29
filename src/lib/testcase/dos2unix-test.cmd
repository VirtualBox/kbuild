@setlocal

@set INSPROG="E:\kBuild\svn\trunk\out\win.amd64\debug\stage\kBuild\bin\win.amd64\kmk_install.exe"
@set TMPFILE="e:\tmp\tmp.txt"
@set CMPPROG="E:\kBuild\svn\trunk\kBuild\bin\win.amd64\kmk_cmp.exe"
@set RMPROG="E:\kBuild\svn\trunk\kBuild\bin\win.amd64\kmk_rm.exe"

@%RMPROG% -f -- %TMPFILE%

@echo ... dos2unix ...
%INSPROG% --dos2unix dos-text.txt %TMPFILE%
@if not errorlevel 0 goto end
%CMPPROG% -- %TMPFILE% unix-text.txt
@if not errorlevel 0 goto end

%INSPROG% --dos2unix unix-text.txt %TMPFILE%
@if not errorlevel 0 goto end
%CMPPROG% -- %TMPFILE% unix-text.txt
@if not errorlevel 0 goto end

%INSPROG% --dos2unix mixed-text.txt %TMPFILE%
@if not errorlevel 0 goto end
%CMPPROG% -- %TMPFILE% unix-text.txt
@if not errorlevel 0 goto end

@echo ... unix2dos ...
%INSPROG% --unix2dos unix-text.txt %TMPFILE%
@if not errorlevel 0 goto end
%CMPPROG% -- %TMPFILE% dos-text.txt
@if not errorlevel 0 goto end

%INSPROG% --unix2dos dos-text.txt %TMPFILE%
@if not errorlevel 0 goto end
%CMPPROG% -- %TMPFILE% dos-text.txt
@if not errorlevel 0 goto end

%INSPROG% --unix2dos mixed-text.txt %TMPFILE%
@if not errorlevel 0 goto end
%CMPPROG% -- %TMPFILE% dos-text.txt
@if not errorlevel 0 goto end


@%RMPROG% -f -- %TMPFILE%
:end
@endlocal

