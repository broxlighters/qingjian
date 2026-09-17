#!/usr/bin/python3
# //! 私有桌面的 Qt 单框、同窗双框、同 PID 双 mapped 窗口。
import json
import os
import sys
from pathlib import Path
from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import QApplication, QLineEdit, QVBoxLayout, QWidget

app = QApplication(sys.argv)
app.setApplicationName('qingjian-qt-test')
app.setDesktopFileName('qingjian-qt-test')
scenario = os.environ.get('QINGJIAN_TEST_SCENARIO', 'single')
entries = [QLineEdit() for _ in range(1 if scenario == 'single' else 2)]
windows = []
if scenario == 'fields':
    window = QWidget()
    layout = QVBoxLayout(window)
    for entry in entries:
        layout.addWidget(entry)
    windows.append(window)
else:
    windows = entries[:]


def record():
    path = Path(sys.argv[1])
    temporary = path.with_suffix('.tmp')
    texts = [entry.text() for entry in entries]
    temporary.write_text(json.dumps({'text': texts[0], 'texts': texts,
        'scale': entries[0].devicePixelRatioF()}, ensure_ascii=False))
    temporary.replace(path)


for entry in entries:
    entry.textChanged.connect(record)
for index, window in enumerate(reversed(windows)):
    window.setWindowTitle(f'QJ isolated Qt {scenario} {index}')
    window.resize(640, 240 if scenario == 'fields' else 180)
    window.show()
entries[0].setFocus()
record()
QTimer.singleShot(45000, app.quit)
app.exec()
