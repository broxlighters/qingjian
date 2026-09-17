#!/usr/bin/python3
# //! Qt6 原生输入测试，仅写固定测试输入状态。
import json,sys
from pathlib import Path
from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import QApplication,QLineEdit
app=QApplication(sys.argv)
app.setApplicationName("qingjian-qt-test")
app.setDesktopFileName("qingjian-qt-test")
entry=QLineEdit()
entry.setWindowTitle("Qingjian isolated Qt6 test")
entry.resize(640,180)
def changed():
 path=Path(sys.argv[1]);tmp=path.with_suffix(".tmp")
 tmp.write_text(json.dumps({"text":entry.text(),"scale":entry.devicePixelRatioF()},ensure_ascii=False))
 tmp.replace(path)
entry.textChanged.connect(changed)
entry.show();entry.setFocus();changed()
QTimer.singleShot(45000,app.quit)
app.exec()
