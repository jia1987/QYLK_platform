// 系统设置面板（原型 ⚙ 面板扩展）：PIN 门禁（决策 D9）+ 时间/医院/串口/绑定维护（决策 D2）
import QtQuick 2.12
import QtQuick.Controls 2.12

Rectangle {
    id: overlay
    color: "#bd05080e"
    visible: false
    property bool unlocked: false
    property int burnSlot: -1
    property string burnStatus: ""
    property var binds: []

    function open() {
        overlay.visible = true;
        overlay.unlocked = !App.pinSet;
        overlay.burnSlot = -1;
        overlay.burnStatus = "";
        reload();
        pinIn.text = ""; pinNew1.text = ""; pinNew2.text = "";
        hospIn.text = App.hospitalText(); deptIn.text = App.departmentText();
        portIn.text = App.serialPortText(); baudIn.text = String(App.baudRate());
        timeIn.text = Qt.formatDateTime(new Date(), "yyyy-MM-ddTHH:mm:ss");
    }
    function close() { overlay.visible = false; overlay.unlocked = false; }
    function reload() { binds = App.bindingInfos(); }

    MouseArea { anchors.fill: parent; onClicked: overlay.close() }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: 660
        height: Math.min(parent.height - 40, content.implicitHeight + 48)
        radius: 22
        color: "#1A1E26"
        border { color: "#39404E"; width: 1 }
        MouseArea { anchors.fill: parent }  // 阻断背景点击

        Text {
            id: ptitle
            anchors { top: parent.top; topMargin: 20; horizontalCenter: parent.horizontalCenter }
            text: "系 统 设 置"
            color: "#ECF0F7"
            font { pixelSize: 20; weight: Font.Black; letterSpacing: 4 }
        }
        Text {
            anchors { top: parent.top; topMargin: 24; right: parent.right; rightMargin: 22 }
            text: "✕"
            color: "#707A8A"
            font.pixelSize: 18
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                onClicked: overlay.close()
            }
        }

        Flickable {
            id: flick
            anchors { top: ptitle.bottom; topMargin: 14; left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 12; leftMargin: 30; rightMargin: 30 }
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: content
                width: parent.width
                spacing: 14

                // ============ PIN 门禁 ============
                Rectangle {
                    width: parent.width
                    height: pinCol.implicitHeight + 28
                    radius: 14
                    color: "#21252C"
                    border { color: "#39404E"; width: 1.5 }
                    visible: !overlay.unlocked
                    Column {
                        id: pinCol
                        anchors { fill: parent; margins: 14 }
                        spacing: 10
                        Text {
                            text: App.pinSet ? "输入 PIN 进入设置（4–6 位）" : "首次使用：设置 PIN（4–6 位，设置/维护入口的门禁）"
                            color: "#A7B0BF"
                            font { pixelSize: 14; weight: Font.Bold }
                        }
                        TextField {
                            id: pinNew1
                            visible: !App.pinSet
                            width: parent.width
                            placeholderText: "新 PIN"
                            echoMode: TextInput.Password
                            inputMethodHints: Qt.ImhDigitsOnly
                            color: "#ECF0F7"
                            font.pixelSize: 15
                            background: Rectangle { color: "#4d000000"; radius: 10; border.color: pinNew1.activeFocus ? "#4DA3FF" : "#39404E" }
                        }
                        TextField {
                            id: pinNew2
                            visible: !App.pinSet
                            width: parent.width
                            placeholderText: "确认新 PIN"
                            echoMode: TextInput.Password
                            inputMethodHints: Qt.ImhDigitsOnly
                            color: "#ECF0F7"
                            font.pixelSize: 15
                            background: Rectangle { color: "#4d000000"; radius: 10; border.color: pinNew2.activeFocus ? "#4DA3FF" : "#39404E" }
                        }
                        TextField {
                            id: pinIn
                            visible: App.pinSet
                            width: parent.width
                            placeholderText: "PIN"
                            echoMode: TextInput.Password
                            inputMethodHints: Qt.ImhDigitsOnly
                            color: "#ECF0F7"
                            font.pixelSize: 15
                            background: Rectangle { color: "#4d000000"; radius: 10; border.color: pinIn.activeFocus ? "#4DA3FF" : "#39404E" }
                        }
                        Rectangle {
                            width: 140; height: 40; radius: 10
                            color: pinOkMa.containsMouse ? "#00b956" : "#00A94F"
                            Text { anchors.centerIn: parent; text: App.pinSet ? "进 入" : "设置并进入"; color: "#fff"; font { pixelSize: 15; weight: Font.Black } }
                            MouseArea {
                                id: pinOkMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (App.pinSet) {
                                        if (App.verifyPin(pinIn.text)) { overlay.unlocked = true; overlay.reload(); }
                                        else pinIn.text = "";
                                    } else {
                                        if (pinNew1.text === pinNew2.text && App.setInitialPin(pinNew1.text))
                                            overlay.unlocked = true;
                                    }
                                }
                            }
                        }
                    }
                }

                // ============ 以下为门禁后内容 ============
                // ---- 时间设置 ----
                GroupBox {
                    width: parent.width
                    visible: overlay.unlocked
                    title: ""
                    padding: 14
                    background: Rectangle { color: "#21252C"; radius: 14; border.color: "#39404E"; border.width: 1.5 }
                    Column {
                        width: parent.width
                        spacing: 10
                        Text { text: "⏰ 时间设置"; color: "#A7B0BF"; font { pixelSize: 14; weight: Font.Black; letterSpacing: 2 } }
                        Row {
                            width: parent.width
                            spacing: 10
                            TextField {
                                id: timeIn
                                width: parent.width - 110
                                color: "#ECF0F7"
                                font { pixelSize: 15; family: "Consolas" }
                                background: Rectangle { color: "#4d000000"; radius: 10; border.color: timeIn.activeFocus ? "#4DA3FF" : "#39404E" }
                            }
                            Rectangle {
                                width: 100; height: 40; radius: 10
                                color: timeMa.containsMouse ? "#5ab0ff" : "#4DA3FF"
                                Text { anchors.centerIn: parent; text: "应用"; color: "#0B1220"; font { pixelSize: 14; weight: Font.Black } }
                                MouseArea { id: timeMa; anchors.fill: parent; hoverEnabled: true; onClicked: App.setSystemTime(timeIn.text) }
                            }
                        }
                        Text { text: "格式 YYYY-MM-DDTHH:MM:SS · 治疗审计时间戳使用单调时钟，改系统时间不污染记录"; color: "#707A8A"; font.pixelSize: 11 }
                    }
                }

                // ---- 医院 / 科室 ----
                GroupBox {
                    width: parent.width
                    visible: overlay.unlocked
                    title: ""
                    padding: 14
                    background: Rectangle { color: "#21252C"; radius: 14; border.color: "#39404E"; border.width: 1.5 }
                    Column {
                        width: parent.width
                        spacing: 10
                        Text { text: "🏥 医院 / 科室"; color: "#A7B0BF"; font { pixelSize: 14; weight: Font.Black; letterSpacing: 2 } }
                        TextField {
                            id: hospIn
                            width: parent.width
                            placeholderText: "医院名称"
                            color: "#ECF0F7"; font.pixelSize: 15
                            background: Rectangle { color: "#4d000000"; radius: 10; border.color: hospIn.activeFocus ? "#4DA3FF" : "#39404E" }
                        }
                        TextField {
                            id: deptIn
                            width: parent.width
                            placeholderText: "科室"
                            color: "#ECF0F7"; font.pixelSize: 15
                            background: Rectangle { color: "#4d000000"; radius: 10; border.color: deptIn.activeFocus ? "#4DA3FF" : "#39404E" }
                        }
                        Rectangle {
                            width: 100; height: 40; radius: 10
                            color: hospMa.containsMouse ? "#5ab0ff" : "#4DA3FF"
                            Text { anchors.centerIn: parent; text: "保存"; color: "#0B1220"; font { pixelSize: 14; weight: Font.Black } }
                            MouseArea { id: hospMa; anchors.fill: parent; hoverEnabled: true; onClicked: App.saveIdentity(hospIn.text, deptIn.text) }
                        }
                    }
                }

                // ---- 串口 ----
                GroupBox {
                    width: parent.width
                    visible: overlay.unlocked
                    title: ""
                    padding: 14
                    background: Rectangle { color: "#21252C"; radius: 14; border.color: "#39404E"; border.width: 1.5 }
                    Column {
                        width: parent.width
                        spacing: 10
                        Text { text: "🔌 串口（当前：" + App.serialConfigText() + "）"; color: "#A7B0BF"; font { pixelSize: 14; weight: Font.Black; letterSpacing: 2 } }
                        Row {
                            width: parent.width
                            spacing: 10
                            TextField {
                                id: portIn
                                width: parent.width - 190
                                placeholderText: "如 COM6 或 /dev/massage485；留空=模拟总线"
                                color: "#ECF0F7"; font { pixelSize: 15; family: "Consolas" }
                                background: Rectangle { color: "#4d000000"; radius: 10; border.color: portIn.activeFocus ? "#4DA3FF" : "#39404E" }
                            }
                            TextField {
                                id: baudIn
                                width: 80
                                color: "#ECF0F7"; font { pixelSize: 15; family: "Consolas" }
                                background: Rectangle { color: "#4d000000"; radius: 10; border.color: baudIn.activeFocus ? "#4DA3FF" : "#39404E" }
                            }
                            Rectangle {
                                width: 90; height: 40; radius: 10
                                color: serMa.containsMouse ? "#5ab0ff" : "#4DA3FF"
                                Text { anchors.centerIn: parent; text: "保存"; color: "#0B1220"; font { pixelSize: 14; weight: Font.Black } }
                                MouseArea { id: serMa; anchors.fill: parent; hoverEnabled: true; onClicked: App.saveSerial(portIn.text, parseInt(baudIn.text) || 57600) }
                            }
                        }
                    }
                }

                // ---- 治疗头绑定 / 维护 ----
                GroupBox {
                    width: parent.width
                    visible: overlay.unlocked
                    title: ""
                    padding: 14
                    background: Rectangle { color: "#21252C"; radius: 14; border.color: "#39404E"; border.width: 1.5 }
                    Column {
                        width: parent.width
                        spacing: 10
                        Text { text: "🔧 治疗头绑定 / 维护（换头烧录 = 0xAA 单头流程）"; color: "#A7B0BF"; font { pixelSize: 14; weight: Font.Black; letterSpacing: 1 } }
                        Repeater {
                            model: overlay.binds
                            Rectangle {
                                width: parent.width
                                height: 44
                                radius: 10
                                color: "#282E39"
                                Row {
                                    anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 12; right: parent.right; rightMargin: 12 }
                                    spacing: 10
                                    Text { text: modelData.slot; color: "#4DA3FF"; font.pixelSize: 16; font.weight: Font.Black; font.family: "Consolas"; width: 34 }
                                    Text { text: "addr " + modelData.addr; color: "#ECF0F7"; font.pixelSize: 14; font.family: "Consolas"; width: 70 }
                                    Rectangle {
                                        width: 10; height: 10; radius: 5
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: modelData.online ? "#22C55E" : "#5A6270"
                                    }
                                    Text { text: modelData.online ? "在线" : "离线"; color: modelData.online ? "#22C55E" : "#707A8A"; font.pixelSize: 12; width: 34 }
                                    Text { text: modelData.type; color: "#A7B0BF"; font.pixelSize: 14; width: 96 }
                                    Rectangle {
                                        width: 76; height: 32; radius: 8
                                        color: typeMa.containsMouse ? "#303744" : "#21252C"
                                        border { color: "#39404E"; width: 1 }
                                        Text { anchors.centerIn: parent; text: "换类型"; color: "#2DD4BF"; font { pixelSize: 12; weight: Font.Bold } }
                                        MouseArea {
                                            id: typeMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: {
                                                var types = ["局部治疗头", "手法治疗头", "靠垫治疗头"];
                                                var cur = types.indexOf(modelData.type);
                                                App.maintSetType(index, types[(cur + 1) % 3]);
                                                overlay.reload();
                                            }
                                        }
                                    }
                                    Rectangle {
                                        width: 96; height: 32; radius: 8
                                        color: burnMa.containsMouse ? "#3a2020" : "#2a1a1c"
                                        border { color: "#8f3a3a"; width: 1 }
                                        Text { anchors.centerIn: parent; text: "烧录到此槽"; color: "#EF4444"; font { pixelSize: 12; weight: Font.Bold } }
                                        MouseArea {
                                            id: burnMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: { overlay.burnSlot = index; overlay.burnStatus = "已选择 " + modelData.slot + "：先「检测新头」再「烧录」"; }
                                        }
                                    }
                                }
                            }
                        }

                        // 烧录向导
                        Rectangle {
                            width: parent.width
                            height: burnWizardCol.implicitHeight + 24
                            radius: 10
                            color: "#241a1c"
                            border { color: "#8f3a3a"; width: 1.5 }
                            visible: overlay.burnSlot >= 0
                            Column {
                                id: burnWizardCol
                                anchors { fill: parent; margins: 12 }
                                spacing: 8
                                Text {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    text: "⚠ 烧录联锁：仅当「新头（地址0）在线」且「其余治疗头全部离线」时才允许执行——0xAA 会被总线上所有头接收！请拔除其余治疗头，只留新头。"
                                    color: "#F5A623"
                                    font.pixelSize: 12
                                    lineHeight: 1.4
                                }
                                Text { text: overlay.burnStatus; color: "#A7B0BF"; font.pixelSize: 13 }
                                Row {
                                    spacing: 10
                                    Rectangle {
                                        width: 110; height: 36; radius: 8
                                        color: probeMa.containsMouse ? "#303744" : "#282E39"
                                        border { color: "#4DA3FF"; width: 1 }
                                        Text { anchors.centerIn: parent; text: "检测新头"; color: "#4DA3FF"; font { pixelSize: 13; weight: Font.Bold } }
                                        MouseArea { id: probeMa; anchors.fill: parent; hoverEnabled: true; onClicked: { overlay.burnStatus = "探测地址 0 …"; App.maintProbeZero(); } }
                                    }
                                    Rectangle {
                                        width: 130; height: 36; radius: 8
                                        color: doBurnMa.containsMouse ? "#e0393f" : "#D32F2F"
                                        Text { anchors.centerIn: parent; text: "执行烧录"; color: "#fff"; font { pixelSize: 13; weight: Font.Black } }
                                        MouseArea { id: doBurnMa; anchors.fill: parent; hoverEnabled: true; onClicked: { overlay.burnStatus = "烧录中 …"; App.maintBurn(overlay.burnSlot); } }
                                    }
                                    Rectangle {
                                        width: 80; height: 36; radius: 8
                                        color: "#21252C"
                                        border { color: "#39404E"; width: 1 }
                                        Text { anchors.centerIn: parent; text: "取消"; color: "#A7B0BF"; font.pixelSize: 13 }
                                        MouseArea { anchors.fill: parent; onClicked: overlay.burnSlot = -1 }
                                    }
                                }
                            }
                        }
                    }
                }

                // ---- 修改 PIN ----
                GroupBox {
                    width: parent.width
                    visible: overlay.unlocked && App.pinSet
                    title: ""
                    padding: 14
                    background: Rectangle { color: "#21252C"; radius: 14; border.color: "#39404E"; border.width: 1.5 }
                    Column {
                        width: parent.width
                        spacing: 10
                        Text { text: "🔑 修改 PIN"; color: "#A7B0BF"; font { pixelSize: 14; weight: Font.Black; letterSpacing: 2 } }
                        Row {
                            spacing: 10
                            TextField { id: chOld; width: 130; placeholderText: "原 PIN"; echoMode: TextInput.Password; color: "#ECF0F7"; font.pixelSize: 14; background: Rectangle { color: "#4d000000"; radius: 10; border.color: "#39404E" } }
                            TextField { id: chNew; width: 130; placeholderText: "新 PIN"; echoMode: TextInput.Password; color: "#ECF0F7"; font.pixelSize: 14; background: Rectangle { color: "#4d000000"; radius: 10; border.color: "#39404E" } }
                            TextField { id: chNew2; width: 130; placeholderText: "确认"; echoMode: TextInput.Password; color: "#ECF0F7"; font.pixelSize: 14; background: Rectangle { color: "#4d000000"; radius: 10; border.color: "#39404E" } }
                            Rectangle {
                                width: 80; height: 40; radius: 10
                                color: chMa.containsMouse ? "#5ab0ff" : "#4DA3FF"
                                Text { anchors.centerIn: parent; text: "修改"; color: "#0B1220"; font { pixelSize: 14; weight: Font.Black } }
                                MouseArea {
                                    id: chMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (chNew.text !== chNew2.text) return;
                                        if (!App.changePin(chOld.text, chNew.text)) chOld.text = "";
                                        chNew.text = ""; chNew2.text = "";
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: App.versionText
                    color: "#707A8A"
                    font { pixelSize: 12; family: "Consolas" }
                }
            }
        }
    }

    Connections {
        target: App
        onMaintZeroFound: {
            overlay.burnStatus = found ? "✔ 检测到地址 0 的新头，可执行烧录"
                                       : "✘ 未检测到地址 0 设备";
        }
        onMaintBurnResult: {
            overlay.burnStatus = ok ? "✔ 烧录成功" : ("✘ " + msg);
            overlay.reload();
        }
    }
}
