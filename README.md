# HLInt
A simple interpreter for the hypothetical language HL, written in C.

## Build
Run `build.bat` (requires Visual Studio's C compiler).

## Run
HLInt PROG1.HL

Writes NOSPACES.TXT and RES_SYM.TXT, then prints ERROR or NO ERROR(S) FOUND and runs the program.

## Optional web UI
```bash
pip install streamlit
streamlit run app.py
```
