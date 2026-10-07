"""Streamlit UI for HLInt.

Put this file in the same folder as HLInt.exe, then run:
    streamlit run app.py
"""
import subprocess
import tempfile
from pathlib import Path

import streamlit as st

HERE = Path(__file__).parent
EXE = HERE / "HLInt.exe"          # the compiled interpreter

SAMPLES = {
    "PROG1.HL": "x: integer;\nx:= 5;\noutput<<x;\n",
    "PROG2.HL": "x: integer;\ny: double;\nx:= 3;\ny:= 1.25;\noutput<<x+y;\n",
    "PROG3.HL": "x: integer;\ny: double;\nx:= 3;\nif(x<5)\n  output<<x;\n",
    "Mixed-case test": (
        'x: integer;\nx:= 6;\nIf(x<5)\nOutput<<X;\noutput<<"hello world";\n'
        "y: double;\ny= 4 + 2.56;\noutput<<y-x;\n"
    ),
    "Error: too many decimals": "y: double;\ny:= 1.234;\n",
    "Error: missing semicolon": "x: integer;\nx:= 5\noutput<<x;\n",
    "Error: bad comparison (=<)": "x: integer;\nif(x=<5)\noutput<<x;\n",
    "Error: unterminated string": 'output<<"hi;\n',
    "Error: double into integer": "x: integer;\nx:= 2.5;\n",
    "Error: unknown type": "x: string;\n",
    "Error: undeclared variable": "x: integer;\nz:= 5;\n",
}


def load_sample():
    """Copy the chosen sample into the editor."""
    st.session_state.source = SAMPLES[st.session_state.sample]


def run_hlint(source: str):
    """Run HLInt.exe on `source` inside a temp folder.

    HLInt writes NOSPACES.TXT and RES_SYM.TXT into its current directory,
    so using a fresh temp folder keeps every run separate and tidy.
    Returns (screen_output, nospaces_text, res_sym_text).
    """
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "prog.HL").write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(EXE), "prog.HL"],
            cwd=tmp, capture_output=True, text=True, timeout=10,
        )

        def read(name):
            p = tmp / name
            return p.read_text() if p.exists() else "(file not created)"

        return result.stdout, read("NOSPACES.TXT"), read("RES_SYM.TXT")


st.set_page_config(page_title="HLInt", page_icon="🖥️", layout="wide")
st.title("HLInt: HL Interpreter")
st.caption("Type an HL program, press Run, and see what HLInt does with it.")

if not EXE.exists():
    st.error(f"HLInt.exe not found next to app.py (looked in {HERE}). Build it first with build.bat.")
    st.stop()

if "source" not in st.session_state:
    st.session_state.source = SAMPLES["PROG1.HL"]

left, right = st.columns(2)

with left:
    st.selectbox("Load a sample", list(SAMPLES), key="sample", on_change=load_sample)
    st.text_area("HL source", key="source", height=320)
    run = st.button("Run", type="primary")

with right:
    if run:
        try:
            screen, nospaces, res_sym = run_hlint(st.session_state.source)
        except subprocess.TimeoutExpired:
            st.error("HLInt took too long and was stopped.")
            st.stop()

        if screen.startswith("ERROR"):
            st.error("Syntax error found")
        else:
            st.success("NO ERROR(S) FOUND")

        tab1, tab2, tab3 = st.tabs(["Screen output", "NOSPACES.TXT", "RES_SYM.TXT"])
        tab1.code(screen, language=None)
        tab2.code(nospaces, language=None)
        tab3.code(res_sym, language=None)
    else:
        st.info("Results will appear here.")
