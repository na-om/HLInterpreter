@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /W4 /D_CRT_SECURE_NO_WARNINGS HLInt.c
del HLInt.obj
