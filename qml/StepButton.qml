// @srs SRS-112
// ▲▼ 步进按钮：按下立即触发，450ms 后以 110ms 连发（原型长按连发行为）
import QtQuick 2.12

Rectangle {
    id: btn
    property alias label: txt.text
    property bool btnEnabled: true
    signal triggered()

    radius: 13
    color: ma.containsMouse && btnEnabled ? "#26313f" : "#282E39"
    border.width: 2
    border.color: ma.containsMouse && btnEnabled ? "#4DA3FF" : "#39404E"
    opacity: btnEnabled ? 1.0 : 0.25

    Text {
        id: txt
        anchors.centerIn: parent
        color: "#4DA3FF"
        font { pixelSize: 19; weight: Font.Black; letterSpacing: 2 }
    }

    Timer {
        id: repTimer
        interval: 110
        repeat: true
        onTriggered: btn.triggered()
    }
    Timer {
        id: delayTimer
        interval: 450
        onTriggered: repTimer.start()
    }

    Behavior on color { ColorAnimation { duration: 100 } }
    Behavior on border.color { ColorAnimation { duration: 100 } }

    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: btn.btnEnabled
        hoverEnabled: true
        onPressed: {
            btn.triggered();
            delayTimer.start();
        }
        onReleased: { delayTimer.stop(); repTimer.stop(); }
        onCanceled: { delayTimer.stop(); repTimer.stop(); }
    }
}
