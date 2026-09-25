// 中央参数调节区（原型 .center）：预设 3 / 模式 2 / 频率时间 5 比例布局 + 锁定层
import QtQuick 2.12

Rectangle {
    id: panel
    radius: 16
    color: "#1A1E26"
    border { color: "#39404E"; width: 1 }

    readonly property var head: App.selectedHead

    // ---- 标题行 ----
    Item {
        id: titleRow
        anchors { top: parent.top; left: parent.left; right: parent.right; topMargin: 10; leftMargin: 14; rightMargin: 14 }
        height: 32
        Text {
            anchors.centerIn: parent
            text: "参 数 调 节 区"
            color: "#ECF0F7"
            font { pixelSize: 18; weight: Font.Black; letterSpacing: 6 }
        }
        Row {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 8
            Text { text: "当前"; color: "#A7B0BF"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: App.targetName
                color: "#F5A623"
                font { pixelSize: 17; weight: Font.Black; family: "Consolas" }
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ---- 参数区 ----
    Column {
        id: paramsCol
        anchors {
            top: titleRow.bottom; topMargin: 8
            left: parent.left; right: parent.right; bottom: parent.bottom
            leftMargin: 14; rightMargin: 14; bottomMargin: 12
        }
        spacing: 8
        opacity: App.centerLocked ? 0.38 : 1.0
        property real availH: height - 2 * spacing

        // ==== 预设区（flex 3）====
        Rectangle {
            width: parent.width
            height: paramsCol.availH * 0.30
            radius: 14
            color: "#21252C"
            border { width: 1.5; color: presetArea.containsMouse ? "#4A5262" : "#39404E" }
            MouseArea { id: presetArea; anchors.fill: parent; hoverEnabled: true; propagateComposedEvents: true }

            Column {
                anchors { fill: parent; margins: 10 }
                spacing: 6
                Item {
                    width: parent.width
                    height: 18
                    Text {
                        anchors.left: parent.left
                        text: "预 设"
                        color: "#A7B0BF"
                        font { pixelSize: 15; weight: Font.Black; letterSpacing: 3 }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "点击应用 · 再点一次存为预设"
                        color: "#707A8A"
                        font { pixelSize: 12; family: "Consolas" }
                    }
                }
                Grid {
                    width: parent.width
                    height: parent.height - 24
                    columns: 3
                    rows: 2
                    columnSpacing: 8
                    rowSpacing: 8
                    Repeater {
                        model: 6
                        Rectangle {
                            width: (parent.width - 16) / 3
                            height: (parent.height - 8) / 2
                            radius: 12
                            readonly property bool pending: App.savePendingIndex === index
                            color: chipMa.containsMouse ? "#303744" : "#282E39"
                            border.width: 2
                            border.color: pending ? "#F5A623" : (chipMa.containsMouse ? "#2DD4BF" : "#39404E")
                            Column {
                                anchors.centerIn: parent
                                spacing: 3
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: pending ? "存为预设" : "预设" + (index + 1)
                                    color: pending ? "#F5A623" : "#A7B0BF"
                                    font { pixelSize: 13; weight: Font.Black; letterSpacing: 2 }
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: App.presetLabels[index] || ""
                                    color: pending ? "#F5A623" : "#ECF0F7"
                                    font { pixelSize: 14; weight: Font.Bold; family: "Consolas" }
                                }
                            }
                            MouseArea {
                                id: chipMa
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !App.centerLocked
                                onClicked: App.applyPreset(index)
                            }
                        }
                    }
                }
            }
        }

        // ==== 模式区（flex 2）====
        Rectangle {
            width: parent.width
            height: paramsCol.availH * 0.20
            radius: 14
            color: "#21252C"
            border { width: 1.5; color: "#39404E" }
            Column {
                anchors { fill: parent; margins: 10 }
                spacing: 6
                Item {
                    width: parent.width
                    height: 18
                    Text {
                        anchors.left: parent.left
                        text: "模 式"
                        color: "#A7B0BF"
                        font { pixelSize: 15; weight: Font.Black; letterSpacing: 3 }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.modeDesc
                        color: "#707A8A"
                        font.pixelSize: 12
                    }
                }
                Row {
                    width: parent.width
                    height: parent.height - 24
                    spacing: 10
                    Repeater {
                        model: 3
                        Rectangle {
                            id: modeBtn
                            width: (parent.width - 20) / 3
                            height: parent.height
                            radius: 16
                            readonly property bool active: panel.head !== null && panel.head.mode === index
                            color: active ? "#33291a" : (modeMa.containsMouse ? "#303744" : "#282E39")
                            border.width: 2
                            border.color: active ? "#F5A623" : (modeMa.containsMouse ? "#4A5262" : "#39404E")
                            Column {
                                anchors.centerIn: parent
                                spacing: 2
                                Waveform {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 52; height: 16
                                    mode: index
                                    lineColor: modeBtn.active ? "#F5A623" : "#5A6270"
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: ["恒频", "扫频", "阶频"][index]
                                    color: modeBtn.active ? "#F5A623" : "#A7B0BF"
                                    font { pixelSize: 17; weight: Font.Black; letterSpacing: 4 }
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: ["定频 直线输出", "0→频率 往复", "爬升·保持·下降"][index]
                                    color: "#707A8A"
                                    font.pixelSize: 10
                                }
                            }
                            MouseArea {
                                id: modeMa
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !App.centerLocked
                                onClicked: App.setMode(index)
                            }
                        }
                    }
                }
            }
        }

        // ==== 频率 / 时间（flex 5）====
        Row {
            width: parent.width
            height: paramsCol.availH * 0.50 - 8
            spacing: 8

            // 频率卡
            Rectangle {
                width: (parent.width - 8) / 2
                height: parent.height
                radius: 14
                color: "#21252C"
                border { width: 1.5; color: "#39404E" }
                Column {
                    anchors { fill: parent; margins: 10 }
                    spacing: 6
                    Item {
                        width: parent.width
                        height: 18
                        Text {
                            anchors.left: parent.left
                            text: "频 率"
                            color: "#A7B0BF"
                            font { pixelSize: 15; weight: Font.Black; letterSpacing: 3 }
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: "10–50Hz·步进5"   // 决策 D1：量程修正（原型 100Hz 为错误）
                            color: "#707A8A"
                            font { pixelSize: 11; family: "Consolas" }
                        }
                    }
                    StepButton {
                        width: parent.width
                        height: (parent.height - 24 - 74 - 12) / 2
                        label: "▲ 频率加"
                        btnEnabled: !App.centerLocked && panel.head && panel.head.freqHz < 50
                        onTriggered: App.adjustFreq(1)
                    }
                    Rectangle {
                        width: parent.width
                        height: 74
                        radius: 12
                        color: "#52000000"
                        border.color: "#14ffffff"
                        Row {
                            anchors.centerIn: parent
                            spacing: 4
                            Text {
                                text: panel.head ? String(panel.head.freqHz) : "--"
                                color: "#ECF0F7"
                                font { pixelSize: 42; weight: Font.Black; family: "Consolas" }
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 12
                                text: "Hz"
                                color: "#707A8A"
                                font { pixelSize: 14; weight: Font.Bold }
                            }
                        }
                    }
                    StepButton {
                        width: parent.width
                        height: (parent.height - 24 - 74 - 12) / 2
                        label: "▼ 频率减"
                        btnEnabled: !App.centerLocked && panel.head && panel.head.freqHz > 10
                        onTriggered: App.adjustFreq(-1)
                    }
                }
            }

            // 时间卡
            Rectangle {
                width: (parent.width - 8) / 2
                height: parent.height
                radius: 14
                color: "#21252C"
                border { width: 1.5; color: "#39404E" }
                Column {
                    anchors { fill: parent; margins: 10 }
                    spacing: 6
                    Item {
                        width: parent.width
                        height: 18
                        Text {
                            anchors.left: parent.left
                            text: "时 间"
                            color: "#A7B0BF"
                            font { pixelSize: 15; weight: Font.Black; letterSpacing: 3 }
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: "5–60min·步进5"
                            color: "#707A8A"
                            font { pixelSize: 11; family: "Consolas" }
                        }
                    }
                    StepButton {
                        width: parent.width
                        height: (parent.height - 24 - 74 - 12) / 2
                        label: "▲ 时间加"
                        btnEnabled: !App.centerLocked && panel.head && panel.head.timeMin < 60
                        onTriggered: App.adjustTime(1)
                    }
                    Rectangle {
                        width: parent.width
                        height: 74
                        radius: 12
                        color: "#52000000"
                        border.color: "#14ffffff"
                        Row {
                            anchors.centerIn: parent
                            spacing: 4
                            Text {
                                text: panel.head ? String(panel.head.timeMin) : "--"
                                color: "#ECF0F7"
                                font { pixelSize: 42; weight: Font.Black; family: "Consolas" }
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 12
                                text: "min"
                                color: "#707A8A"
                                font { pixelSize: 14; weight: Font.Bold }
                            }
                        }
                    }
                    StepButton {
                        width: parent.width
                        height: (parent.height - 24 - 74 - 12) / 2
                        label: "▼ 时间减"
                        btnEnabled: !App.centerLocked && panel.head && panel.head.timeMin > 5
                        onTriggered: App.adjustTime(-1)
                    }
                }
            }
        }
    }

    // ---- 锁定层（未选中治疗头）----
    Rectangle {
        anchors { fill: parent; margins: 1 }
        radius: 15
        visible: App.centerLocked
        color: "#8c1a1e26"
        Rectangle {
            anchors.centerIn: parent
            width: lockTip.implicitWidth + 56
            height: lockTip.implicitHeight + 28
            radius: 14
            color: "#d912151c"
            border { width: 2; color: "#39404E" }
            Text {
                id: lockTip
                anchors.centerIn: parent
                text: "☝ 请先点选左侧或右侧治疗头"
                color: "#A7B0BF"
                font { pixelSize: 17; weight: Font.Bold; letterSpacing: 2 }
            }
        }
        MouseArea { anchors.fill: parent }  // 拦截点击
    }
}
