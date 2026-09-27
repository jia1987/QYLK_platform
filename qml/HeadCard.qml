// @srs SRS-111 SRS-112
// 治疗头卡片（原型 .head）：状态着色/选中指示/波形/按钮组/进度条
import QtQuick 2.12

Rectangle {
    id: card
    property var head              // HeadItem
    property bool isLeft: true
    property bool cardEnabled: head !== null && head.stateClass !== "off"

    readonly property color sideColor: isLeft ? "#4DA3FF" : "#2DD4BF"
    readonly property color baseBg: isLeft ? "#1C2740" : "#173532"
    readonly property string st: head ? head.stateClass : "off"

    radius: 14
    color: baseBg
    clip: false
    opacity: st === "off" ? 0.5 : 1.0
    border.width: 2
    border.color: {
        if (st === "off") return "#39404E";
        if (head && head.selected) return sideColor;
        if (st === "fault") return "#EF4444";
        return "transparent";
    }

    // 运行/暂停背景着色（原型 linear-gradient 近似）
    Rectangle {
        anchors { fill: parent; margins: 1 }
        radius: 13
        visible: card.st === "run" || card.st === "pause" || card.st === "starting"
        gradient: Gradient {
            GradientStop {
                position: 0
                color: card.st === "pause" ? "#3d3a2a" : "#253527"
            }
            GradientStop { position: 0.7; color: "transparent" }
        }
    }

    // 选中指示箭头（原型 .head.sel::after）
    Canvas {
        visible: head && head.selected && card.st !== "off"
        width: 16; height: 30
        x: card.isLeft ? card.width + 2 : -18
        anchors.verticalCenter: parent.verticalCenter
        z: 5
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            ctx.fillStyle = String(card.sideColor);
            ctx.beginPath();
            if (card.isLeft) { ctx.moveTo(0, 0); ctx.lineTo(0, 30); ctx.lineTo(16, 15); }
            else { ctx.moveTo(16, 0); ctx.lineTo(16, 30); ctx.lineTo(0, 15); }
            ctx.closePath();
            ctx.fill();
        }
    }

    // 点击选中 / 双击 暂停-继续切换（原型交互）
    MouseArea {
        anchors.fill: parent
        enabled: card.st !== "off"
        onClicked: App.selectHead(card.head.slotId)
        onDoubleClicked: {
            if (card.st === "run") App.headAction(card.head.slotId, "pause");
            else if (card.st === "pause") App.headAction(card.head.slotId, "resume");
        }
    }

    Column {
        anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 8; bottomMargin: 8 }
        spacing: 4
        visible: card.st !== "off"

        // ---- 顶行：编号 + 状态 ----
        Item {
            width: parent.width
            height: 34
            Rectangle {
                id: idChip
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                width: idTxt.implicitWidth + 22
                height: 30
                radius: 9
                color: Qt.rgba(card.sideColor.r, card.sideColor.g, card.sideColor.b, 0.12)
                border.color: Qt.rgba(card.sideColor.r, card.sideColor.g, card.sideColor.b, 0.35)
                Text {
                    id: idTxt
                    anchors.centerIn: parent
                    text: card.head ? card.head.slotId : ""
                    color: card.sideColor
                    font { pixelSize: 20; weight: Font.Black; family: "Consolas" }
                }
            }
            Row {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 7
                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: card.st === "run" ? "#22C55E" : (card.st === "pause" ? "#F5A623"
                           : (card.st === "fault" ? "#EF4444" : (card.st === "starting" || card.st === "stopping" ? "#4DA3FF" : "#707A8A")))
                    SequentialAnimation on opacity {
                        running: card.st === "run"
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 700 }
                        NumberAnimation { to: 1.0; duration: 700 }
                    }
                }
                Text {
                    text: card.head ? (card.st === "fault" && card.head.faultText !== ""
                                       ? "故障 · " + card.head.faultText : card.head.stateText) : ""
                    anchors.verticalCenter: parent.verticalCenter
                    color: card.st === "run" ? "#22C55E" : (card.st === "pause" ? "#F5A623"
                           : (card.st === "fault" ? "#EF4444" : "#A7B0BF"))
                    font { pixelSize: 14; weight: Font.Bold }
                }
            }
        }

        // ---- 中部：参数显示 ----
        Row {
            width: parent.width
            height: 52
            spacing: 16
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Text {
                    text: card.head ? String(card.head.freqHz) : "--"
                    color: "#ECF0F7"
                    font { pixelSize: 30; weight: Font.Black; family: "Consolas" }
                }
                Text {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    text: "Hz"
                    color: "#707A8A"
                    font.pixelSize: 13
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: card.head ? card.head.modeName : ""
                color: "#A7B0BF"
                font { pixelSize: 15; weight: Font.DemiBold }
            }
            Item { width: 1; height: 1 }  // spacer-ish
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (!card.head) return "";
                    if (card.st === "idle" || card.st === "off")
                        return card.head.timeMin + " min";
                    return card.head.remainingText;
                }
                color: card.st === "run" || card.st === "starting" ? "#EF4444"
                       : (card.st === "pause" || card.st === "stopping" ? "#F5A623" : "#ECF0F7")
                font {
                    pixelSize: (card.st === "idle" || card.st === "off") ? 30 : 32
                    weight: Font.Black
                    family: "Consolas"
                }
            }
            // 实际转速（运行中显示，治疗师反馈）
            Text {
                visible: card.st === "run" || card.st === "starting" || card.st === "stopping"
                anchors.verticalCenter: parent.verticalCenter
                text: card.head ? card.head.actualHz + " Hz实际" : ""
                color: "#707A8A"
                font { pixelSize: 12; family: "Consolas" }
            }
            Text {
                visible: card.st === "fault"
                anchors.verticalCenter: parent.verticalCenter
                text: card.head ? card.head.tempC + "℃" : ""
                color: "#F5A623"
                font { pixelSize: 13; family: "Consolas" }
            }
        }

        // ---- 主体：波形 + 按钮 ----
        Row {
            width: parent.width
            height: Math.max(48, (card.height - 34 - 52 - 8 - 16 - 5) * 0.9)
            spacing: 10

            Rectangle {
                width: parent.parent.width * 0.38
                height: parent.height
                radius: 10
                color: "#66000000"
                border.color: "#12ffffff"
                Waveform {
                    anchors { fill: parent; margins: 4 }
                    mode: card.head ? card.head.mode : 0
                    dim: card.st === "idle"
                    lineColor: card.st === "pause" || card.st === "stopping" ? "#6B7480"
                               : (card.st === "run" || card.st === "starting" ? "#22C55E" : card.sideColor)
                }
            }

            Item {
                width: parent.parent.width * 0.62 - 10
                height: parent.height

                // 待机：单独启动（仅选中可用）
                Rectangle {
                    anchors.fill: parent
                    visible: card.st === "idle"
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#16A34A" }
                        GradientStop { position: 1; color: "#00A94F" }
                    }
                    opacity: card.head && card.head.selected ? 1.0 : 0.32
                    Text {
                        anchors.centerIn: parent
                        text: "▶ 单独启动"
                        color: "#FFFFFF"
                        font { pixelSize: 19; weight: Font.Black; letterSpacing: 2 }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: card.head && card.head.selected
                        onClicked: App.requestStart(card.head.slotId)
                    }
                }

                // 运行/暂停：左=暂停/继续 右=停止
                Row {
                    anchors.fill: parent
                    spacing: 8
                    visible: card.st === "run" || card.st === "pause" || card.st === "starting"
                    Rectangle {
                        width: (parent.width - 8) / 2
                        height: parent.height
                        radius: 14
                        color: card.st === "pause" ? "#1a2e22" : "#2b2416"
                        border.width: 2
                        border.color: card.st === "pause" ? "#8a6a2f" : "#8a6a2f"
                        Text {
                            anchors.centerIn: parent
                            text: card.st === "pause" ? "▶ 继续" : "⏸ 暂停"
                            color: card.st === "pause" ? "#22C55E" : "#F5A623"
                            font { pixelSize: 17; weight: Font.Black }
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: card.st === "run" || card.st === "pause"
                            onClicked: App.headAction(card.head.slotId,
                                                      card.st === "pause" ? "resume" : "pause")
                        }
                    }
                    Rectangle {
                        width: (parent.width - 8) / 2
                        height: parent.height
                        radius: 14
                        color: "#241a1c"
                        border.width: 2
                        border.color: "#8f3a3a"
                        Text {
                            anchors.centerIn: parent
                            text: "■ 停止"
                            color: "#EF4444"
                            font { pixelSize: 17; weight: Font.Black }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: App.requestStop(card.head.slotId)
                        }
                    }
                }

                // 停止中
                Rectangle {
                    anchors.fill: parent
                    visible: card.st === "stopping"
                    radius: 14
                    color: "#21252C"
                    border { width: 2; color: "#39404E" }
                    Text {
                        anchors.centerIn: parent
                        text: "停止中…"
                        color: "#707A8A"
                        font { pixelSize: 17; weight: Font.Bold }
                    }
                }

                // 故障：复位按钮（决策 D3：手动复位后才能重启）
                Rectangle {
                    anchors.fill: parent
                    visible: card.st === "fault"
                    radius: 14
                    color: "#2a1a1c"
                    border { width: 2; color: "#D32F2F" }
                    Text {
                        anchors.centerIn: parent
                        text: "⟳ 故障复位"
                        color: "#EF4444"
                        font { pixelSize: 18; weight: Font.Black }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: App.headAction(card.head.slotId, "reset")
                    }
                }
            }
        }

        // ---- 进度条 ----
        Rectangle {
            width: parent.width
            height: 5
            radius: 2.5
            color: "#14ffffff"
            visible: card.st !== "idle"
            Rectangle {
                height: parent.height
                radius: 2.5
                width: parent.width * (card.head ? card.head.progress : 0)
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#00A94F" }
                    GradientStop { position: 1; color: "#22C55E" }
                }
                Behavior on width { NumberAnimation { duration: 900; easing.type: Easing.Linear } }
            }
        }
    }

    // ---- 未连接卡片 ----
    Column {
        anchors.centerIn: parent
        visible: card.st === "off"
        spacing: 6
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: card.head ? card.head.slotId : ""
            color: "#707A8A"
            font { pixelSize: 20; weight: Font.Black; family: "Consolas" }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "未识别到治疗头"
            color: "#707A8A"
            font { pixelSize: 14; weight: Font.DemiBold }
        }
    }
}
