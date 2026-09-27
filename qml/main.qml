// @srs SRS-110 SRS-111 SRS-113 SRS-116 SRS-117 SRS-118
// 台式按摩仪多设备控制软件 —— 主界面
// 1:1 还原原型「台式设备交互界面-单文件离线版.html」布局（量程已按决策 D1 修正）
import QtQuick 2.12
import QtQuick.Window 2.12

Window {
    id: win
    visible: true
    width: 1280
    height: 800
    minimumWidth: 1100
    minimumHeight: 700
    color: "#12151C"
    title: "Shudot CVT Therapy · 三维螺旋振动治疗系统"

    readonly property var allHeads: App.heads
    readonly property var headsL: allHeads.filter(function (h) { return h.side === "L"; })
    readonly property var headsR: allHeads.filter(function (h) { return h.side === "R"; })

    function workingCount(list) {
        var n = 0;
        for (var i = 0; i < list.length; ++i)
            if (list[i].stateClass === "run" || list[i].stateClass === "starting")
                ++n;
        return n;
    }

    // ================= Header =================
    Rectangle {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 10 }
        height: 54
        radius: 14
        color: "#1A1E26"
        border { color: "#39404E"; width: 1 }
        z: 10

        Row {
            id: brand
            anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
            spacing: 12
            Rectangle {
                width: 34; height: 34; radius: 10
                anchors.verticalCenter: parent.verticalCenter
                gradient: Gradient {
                    GradientStop { position: 0; color: "#4DA3FF" }
                    GradientStop { position: 1; color: "#2DD4BF" }
                }
                Text {
                    anchors.centerIn: parent
                    text: "SD"
                    color: "#0B1220"
                    font { pixelSize: 15; weight: Font.Black }
                }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text { text: "Shudot CVT Therapy"; color: "#ECF0F7"; font { pixelSize: 18; weight: Font.Bold } }
                Text { text: "三维螺旋振动治疗系统 · 离线版"; color: "#A7B0BF"; font.pixelSize: 11 }
            }
        }

        Text {
            id: hospInfo
            anchors { left: brand.right; leftMargin: 18; verticalCenter: parent.verticalCenter }
            text: App.hospInfo
            color: "#707A8A"
            font { pixelSize: 13; weight: Font.DemiBold; letterSpacing: 1 }
        }

        // 系统状态胶囊
        Rectangle {
            id: sysPill
            anchors { left: hospInfo.right; leftMargin: 18; verticalCenter: parent.verticalCenter }
            width: sysRow.implicitWidth + 30
            height: 28
            radius: 14
            color: "transparent"
            border.width: 1
            border.color: App.sysStateClass === "running" ? "#6622C55E"
                          : (App.sysStateClass === "paused" ? "#66F5A623" : "#39404E")
            Row {
                id: sysRow
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    id: sysDot
                    width: 10; height: 10; radius: 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: App.sysStateClass === "running" ? "#22C55E"
                           : (App.sysStateClass === "paused" ? "#F5A623" : "#707A8A")
                    SequentialAnimation on opacity {
                        running: App.sysStateClass === "running"
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 800 }
                        NumberAnimation { to: 1.0; duration: 800 }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.sysStateText
                    color: App.sysStateClass === "running" ? "#22C55E"
                           : (App.sysStateClass === "paused" ? "#F5A623" : "#A7B0BF")
                    font { pixelSize: 14; weight: Font.Bold }
                }
            }
        }

        // 设置齿轮（M3.5 接入 PIN 设置面板，当前显示版本信息）
        Rectangle {
            id: gear
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            width: 40; height: 40; radius: 10
            color: "#21252C"
            border { width: 1.5; color: gearMa.containsMouse ? "#4DA3FF" : "#39404E" }
            Text {
                anchors.centerIn: parent
                text: "⚙"
                color: gearMa.containsMouse ? "#4DA3FF" : "#A7B0BF"
                font.pixelSize: 20
            }
            MouseArea {
                id: gearMa
                anchors.fill: parent
                hoverEnabled: true
                onClicked: settingsOverlay.open()
            }
        }

        Text {
            id: clock
            anchors { right: gear.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
            text: App.clockText
            color: "#A7B0BF"
            font { pixelSize: 15; family: "Consolas"; letterSpacing: 1 }
        }
    }

    // ================= 主区：左 3 / 中 4 / 右 3 =================
    Item {
        id: mainArea
        anchors {
            top: header.bottom; topMargin: 10
            left: parent.left; right: parent.right
            leftMargin: 14; rightMargin: 14
            bottom: actionBar.top; bottomMargin: 10
        }
        property real gap: 18
        property real sideW: (width - 2 * gap) * 0.3

        // ---- 左侧 ----
        Rectangle {
            id: sideL
            width: mainArea.sideW
            anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
            radius: 16
            color: "#182233"
            border { color: "#40354f73"; width: 1 }
            property real cardH: (height - 8 - 10 - 30 - 2 * 8) / 3

            Item {
                id: titleL
                anchors { top: parent.top; left: parent.left; right: parent.right; topMargin: 8; leftMargin: 12; rightMargin: 12 }
                height: 30
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Text { text: "L"; color: "#4DA3FF"; font { pixelSize: 22; weight: Font.Black; family: "Consolas" } }
                    Text { text: "左侧治疗头"; color: "#4DA3FF"; font.pixelSize: 15; font.weight: Font.Black; font.letterSpacing: 2; anchors.verticalCenter: parent.verticalCenter }
                }
                Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: {
                        var n = workingCount(headsL);
                        return n ? n + " 工作中" : "";
                    }
                    color: "#A7B0BF"
                    font { pixelSize: 13; weight: Font.DemiBold; family: "Consolas" }
                }
            }
            Column {
                anchors { top: titleL.bottom; topMargin: 8; left: parent.left; right: parent.right; leftMargin: 10; rightMargin: 10 }
                spacing: 8
                Repeater {
                    model: headsL
                    HeadCard {
                        width: sideL.width - 20
                        height: sideL.cardH
                        head: modelData
                        isLeft: true
                    }
                }
            }
        }

        // ---- 中央 ----
        CenterPanel {
            id: center
            anchors {
                top: parent.top; bottom: parent.bottom
                left: sideL.right; right: sideR.left
                leftMargin: mainArea.gap; rightMargin: mainArea.gap
            }
        }

        // ---- 右侧 ----
        Rectangle {
            id: sideR
            width: mainArea.sideW
            anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
            radius: 16
            color: "#152B2A"
            border { color: "#402dd4bf"; width: 1 }
            property real cardH: (height - 8 - 10 - 30 - 2 * 8) / 3

            Item {
                id: titleR
                anchors { top: parent.top; left: parent.left; right: parent.right; topMargin: 8; leftMargin: 12; rightMargin: 12 }
                height: 30
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Text { text: "R"; color: "#2DD4BF"; font { pixelSize: 22; weight: Font.Black; family: "Consolas" } }
                    Text { text: "右侧治疗头"; color: "#2DD4BF"; font.pixelSize: 15; font.weight: Font.Black; font.letterSpacing: 2; anchors.verticalCenter: parent.verticalCenter }
                }
                Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: {
                        var n = workingCount(headsR);
                        return n ? n + " 工作中" : "";
                    }
                    color: "#A7B0BF"
                    font { pixelSize: 13; weight: Font.DemiBold; family: "Consolas" }
                }
            }
            Column {
                anchors { top: titleR.bottom; topMargin: 8; left: parent.left; right: parent.right; leftMargin: 10; rightMargin: 10 }
                spacing: 8
                Repeater {
                    model: headsR
                    HeadCard {
                        width: sideR.width - 20
                        height: sideR.cardH
                        head: modelData
                        isLeft: false
                    }
                }
            }
        }
    }

    // ================= 底部操作栏 =================
    Row {
        id: actionBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 14; rightMargin: 14; bottomMargin: 10 }
        height: 76
        spacing: 16

        Rectangle {
            width: (actionBar.width - 16) / 2
            height: 76
            radius: 14
            color: stopAllMa.containsMouse && App.anyOutput ? "#241a1c" : "transparent"
            border { width: 2.5; color: App.anyOutput ? "#99EF4444" : "#39404E" }
            opacity: App.anyOutput ? 1.0 : 0.3
            Text {
                anchors.centerIn: parent
                text: "■ 全部停止"
                color: "#EF4444"
                font { pixelSize: 22; weight: Font.Black; letterSpacing: 8 }
            }
            MouseArea {
                id: stopAllMa
                anchors.fill: parent
                hoverEnabled: true
                enabled: App.anyOutput
                onClicked: App.requestStopAll()
            }
        }

        Rectangle {
            width: (actionBar.width - 16) / 2
            height: 76
            radius: 14
            color: pauseAllMa.containsMouse && App.anyOutput ? "#2a2620" : "transparent"
            border { width: 2.5; color: App.anyOutput ? (pauseAllMa.containsMouse ? "#F5A623" : "#39404E") : "#39404E" }
            opacity: App.anyOutput ? 1.0 : 0.3
            Text {
                anchors.centerIn: parent
                text: App.anyRunning ? "⏸ 全部暂停" : (App.allPaused ? "▶ 全部继续" : "⏸ 全部暂停 / 继续")
                color: App.anyOutput ? (App.anyRunning ? "#F5A623" : "#A7B0BF") : "#707A8A"
                font { pixelSize: 22; weight: Font.Black; letterSpacing: 4 }
            }
            MouseArea {
                id: pauseAllMa
                anchors.fill: parent
                hoverEnabled: true
                enabled: App.anyOutput
                onClicked: App.requestPauseAll()
            }
        }
    }

    // ================= Toast =================
    Item {
        id: toastArea
        anchors { top: parent.top; topMargin: 72; horizontalCenter: parent.horizontalCenter }
        width: parent.width
        height: 220
        z: 200

        ListModel { id: toastModel }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Repeater {
                model: toastModel
                Rectangle {
                    property color tc: model.type === "ok" ? "#22C55E"
                                       : (model.type === "warn" ? "#F5A623"
                                       : (model.type === "err" ? "#EF4444" : "#ECF0F7"))
                    width: toastTxt.implicitWidth + 52
                    height: 40
                    radius: 20
                    color: "#2A3140"
                    border.color: Qt.rgba(tc.r, tc.g, tc.b, 0.5)
                    opacity: toastFade.opacityValue
                    SequentialAnimation {
                        id: toastFade
                        property real opacityValue: 0
                        running: true
                        NumberAnimation { target: toastFade; property: "opacityValue"; from: 0; to: 1; duration: 200 }
                        PauseAnimation { duration: Math.max(400, model.ms - 700) }
                        NumberAnimation { target: toastFade; property: "opacityValue"; to: 0; duration: 500 }
                    }
                    Text {
                        id: toastTxt
                        anchors.centerIn: parent
                        text: model.msg
                        color: parent.tc
                        font { pixelSize: 15; weight: Font.Bold }
                    }
                    Timer {
                        interval: model.ms
                        running: true
                        onTriggered: toastModel.remove(index)
                    }
                }
            }
        }

        Connections {
            target: App
            onToast: {
                toastModel.append({ msg: msg, type: type, ms: ms });
                if (toastModel.count > 4)
                    toastModel.remove(0);
            }
        }
    }

    // ================= 设置面板（PIN 门禁 + 绑定/维护） =================
    SettingsOverlay {
        id: settingsOverlay
        anchors.fill: parent
        z: 250
    }

    // ================= 确认弹窗 =================
    Rectangle {
        id: confirmMask
        anchors.fill: parent
        color: "#bd05080e"
        visible: false
        z: 300
        property var info: null

        MouseArea { anchors.fill: parent }  // 阻断底层交互

        Rectangle {
            id: modalBox
            anchors.centerIn: parent
            width: Math.min(690, parent.width - 80)
            height: modalCol.implicitHeight + 72
            radius: 24
            color: "#1A1E26"
            border { color: "#39404E"; width: 1 }
            scale: confirmMask.visible ? 1.0 : 0.9
            opacity: confirmMask.visible ? 1.0 : 0.0
            Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutQuad } }
            Behavior on opacity { NumberAnimation { duration: 220 } }

            Column {
                id: modalCol
                anchors { top: parent.top; topMargin: 36; left: parent.left; right: parent.right; leftMargin: 44; rightMargin: 44 }
                spacing: 16

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: confirmMask.info ? confirmMask.info.icon : ""
                    font.pixelSize: 56
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: confirmMask.info ? confirmMask.info.title : ""
                    color: "#ECF0F7"
                    font { pixelSize: 26; weight: Font.Bold }
                    wrapMode: Text.Wrap
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: confirmMask.info ? confirmMask.info.msg : ""
                    color: "#A7B0BF"
                    font.pixelSize: 19
                    lineHeight: 1.5
                    wrapMode: Text.Wrap
                }
                Row {
                    width: parent.width
                    height: 64
                    spacing: 20
                    topPadding: 10
                    Rectangle {
                        width: (parent.width - 20) / 2
                        height: 64
                        radius: 16
                        color: cancelMa.containsMouse ? "#2A3140" : "transparent"
                        border { width: 2.5; color: "#39404E" }
                        Text {
                            anchors.centerIn: parent
                            text: confirmMask.info ? confirmMask.info.cancelText : "取消"
                            color: "#A7B0BF"
                            font { pixelSize: 22; weight: Font.Black; letterSpacing: 3 }
                        }
                        MouseArea {
                            id: cancelMa
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: { confirmMask.visible = false; App.confirmResponse(false); }
                        }
                    }
                    Rectangle {
                        width: (parent.width - 20) / 2
                        height: 64
                        radius: 16
                        readonly property bool danger: confirmMask.info ? confirmMask.info.danger : false
                        color: danger ? (okMa.containsMouse ? "#e0393f" : "#D32F2F")
                                      : (okMa.containsMouse ? "#00b956" : "#00A94F")
                        Text {
                            anchors.centerIn: parent
                            text: confirmMask.info ? confirmMask.info.okText : "确认"
                            color: "#FFFFFF"
                            font { pixelSize: 22; weight: Font.Black; letterSpacing: 3 }
                        }
                        MouseArea {
                            id: okMa
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: { confirmMask.visible = false; App.confirmResponse(true); }
                        }
                    }
                }
            }
        }

        Connections {
            target: App
            onConfirmRequest: {
                confirmMask.info = info;
                confirmMask.visible = true;
            }
        }
    }
}
