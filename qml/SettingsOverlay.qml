// @srs SRS-060 SRS-070 SRS-074 SRS-097 SRS-099 SRS-103 SRS-114 SRS-115
// 系统设置面板（原型 ⚙ 面板扩展）：PIN 门禁（决策 D9）+ 时间/医院/串口/绑定维护（决策 D2）
// + M4b：操作员管理（D14）、审计记录查询（D23）、审计导出（D22）
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
    // ---- M4b 状态 ----
    property var ops: []                 // 操作员名单
    property var auditRows: []           // 当前页审计事件
    property string auditCat: ""         // 类别过滤（""=全部）
    property int auditPageIdx: 0
    property int auditTotal: 0
    readonly property int auditPageSize: 30
    property var exportDevs: []          // 可写挂载卷（U盘）
    property int exportSel: -1
    property int exportRange: 0          // 0全量 1近30天 2近90天
    property string exportStatus: ""

    function open() {
        overlay.visible = true;
        overlay.unlocked = !App.pinSet;
        overlay.burnSlot = -1;
        overlay.burnStatus = "";
        overlay.auditPageIdx = 0;
        overlay.exportSel = -1;
        overlay.exportStatus = "";
        reload();
        reloadOps();
        reloadAudit();
        reloadExports();
        pinIn.text = ""; pinNew1.text = ""; pinNew2.text = "";
        hospIn.text = App.hospitalText(); deptIn.text = App.departmentText();
        portIn.text = App.serialPortText(); baudIn.text = String(App.baudRate());
        timeIn.text = Qt.formatDateTime(new Date(), "yyyy-MM-ddTHH:mm:ss");
    }
    function close() { overlay.visible = false; overlay.unlocked = false; }
    function reload() { binds = App.bindingInfos(); }
    function reloadOps() { ops = App.operatorList(); }
    function reloadAudit() {
        auditTotal = App.auditCount(auditCat);
        auditRows = App.auditPage(auditPageIdx * auditPageSize, auditPageSize, auditCat);
    }
    function reloadExports() { exportDevs = App.exportTargets(); }

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

                // ---- 操作员（M4b · D14）----
                GroupBox {
                    width: parent.width
                    visible: overlay.unlocked
                    title: ""
                    padding: 14
                    background: Rectangle { color: "#21252C"; radius: 14; border.color: "#39404E"; border.width: 1.5 }
                    Column {
                        width: parent.width
                        spacing: 10
                        Text { text: "👤 操作员（治疗记录归属 · 名单变更入审计）"; color: "#A7B0BF"; font.pixelSize: 14; font.weight: Font.Black; font.letterSpacing: 1 }
                        Row {
                            spacing: 10
                            TextField {
                                id: opName
                                width: 170
                                placeholderText: "姓名"
                                color: "#ECF0F7"; font.pixelSize: 14
                                background: Rectangle { color: "#4d000000"; radius: 10; border.color: opName.activeFocus ? "#4DA3FF" : "#39404E" }
                            }
                            TextField {
                                id: opCode
                                width: 140
                                placeholderText: "工号（可选）"
                                color: "#ECF0F7"; font.pixelSize: 14
                                background: Rectangle { color: "#4d000000"; radius: 10; border.color: opCode.activeFocus ? "#4DA3FF" : "#39404E" }
                            }
                            Rectangle {
                                width: 90; height: 40; radius: 10
                                color: opAddMa.containsMouse ? "#5ab0ff" : "#4DA3FF"
                                Text { anchors.centerIn: parent; text: "添加"; color: "#0B1220"; font.pixelSize: 14; font.weight: Font.Black }
                                MouseArea {
                                    id: opAddMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (opName.text.trim().length === 0) return;
                                        var nid = App.addOperator(opName.text.trim(), opCode.text.trim());
                                        if (nid > 0) { opName.text = ""; opCode.text = ""; }
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: overlay.ops
                            Rectangle {
                                width: parent.width
                                height: 42
                                radius: 10
                                color: modelData.current ? "#1D3A5C" : "#282E39"
                                border { color: modelData.current ? "#4DA3FF" : "#39404E"; width: 1 }
                                Row {
                                    anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 12; right: parent.right; rightMargin: 12 }
                                    spacing: 10
                                    Text {
                                        text: modelData.name + (modelData.code.length > 0 ? "（" + modelData.code + "）" : "")
                                        color: "#ECF0F7"
                                        font.pixelSize: 14
                                        width: 200
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: modelData.current ? "● 当前" : ""
                                        color: "#4DA3FF"
                                        font.pixelSize: 12
                                        font.weight: Font.Black
                                        width: 50
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Rectangle {
                                        width: 84; height: 30; radius: 8
                                        visible: !modelData.current
                                        color: opSelMa.containsMouse ? "#303744" : "#21252C"
                                        border { color: "#4DA3FF"; width: 1 }
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text { anchors.centerIn: parent; text: "选为当前"; color: "#4DA3FF"; font.pixelSize: 12; font.weight: Font.Bold }
                                        MouseArea { id: opSelMa; anchors.fill: parent; hoverEnabled: true; onClicked: App.setCurrentOperator(modelData.id) }
                                    }
                                    Rectangle {
                                        width: 60; height: 30; radius: 8
                                        color: opDelMa.containsMouse ? "#3a2020" : "#2a1a1c"
                                        border { color: "#8f3a3a"; width: 1 }
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text { anchors.centerIn: parent; text: "移除"; color: "#EF4444"; font.pixelSize: 12; font.weight: Font.Bold }
                                        MouseArea { id: opDelMa; anchors.fill: parent; hoverEnabled: true; onClicked: App.removeOperator(modelData.id) }
                                    }
                                }
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: "当前操作员：" + (App.currentOperatorName.length > 0 ? App.currentOperatorName : "未指定") +
                                  " —— 显示于启动确认弹窗，并写入每条治疗审计（未指定不阻止启动）"
                            color: "#707A8A"
                            font.pixelSize: 11
                        }
                    }
                }

                // ---- 审计记录（M4b · D23）----
                GroupBox {
                    width: parent.width
                    visible: overlay.unlocked
                    title: ""
                    padding: 14
                    background: Rectangle { color: "#21252C"; radius: 14; border.color: "#39404E"; border.width: 1.5 }
                    Column {
                        width: parent.width
                        spacing: 10
                        Text { text: "📜 审计记录（只增不删 · 触发器+链式哈希防篡改 · D21）"; color: "#A7B0BF"; font.pixelSize: 14; font.weight: Font.Black; font.letterSpacing: 1 }
                        Row {
                            spacing: 8
                            Rectangle { width: 10; height: 10; radius: 5; anchors.verticalCenter: parent.verticalCenter; color: App.auditOk ? "#22C55E" : "#EF4444" }
                            Text {
                                text: App.auditOk ? "审计正常" : "⚠ 审计降级：文件兜底记录中（D13），恢复后自动回填"
                                color: App.auditOk ? "#22C55E" : "#EF4444"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                            }
                        }
                        Flow {
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: [["", "全部"], ["therapy", "治疗"], ["fault", "故障"], ["presence", "在位"],
                                        ["access", "访问"], ["config", "配置"], ["clock", "时钟"], ["session", "会话"], ["audit", "审计"]]
                                Rectangle {
                                    width: catTxt.implicitWidth + 22
                                    height: 28
                                    radius: 8
                                    color: overlay.auditCat === modelData[0] ? "#4DA3FF" : "#282E39"
                                    border { color: "#39404E"; width: 1 }
                                    Text {
                                        id: catTxt
                                        anchors.centerIn: parent
                                        text: modelData[1]
                                        color: overlay.auditCat === modelData[0] ? "#0B1220" : "#A7B0BF"
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            overlay.auditCat = modelData[0];
                                            overlay.auditPageIdx = 0;
                                            overlay.reloadAudit();
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle {
                            width: parent.width
                            height: 260
                            radius: 10
                            color: "#12151B"
                            border { color: "#39404E"; width: 1 }
                            ListView {
                                id: auditList
                                anchors { fill: parent; margins: 8 }
                                clip: true
                                model: overlay.auditRows
                                spacing: 4
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                delegate: Column {
                                    width: auditList.width
                                    spacing: 0
                                    Text {
                                        width: parent.width
                                        text: modelData.line
                                        color: "#C9D2DF"
                                        font.pixelSize: 11
                                        font.family: "Consolas"
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: modelData.payload
                                        color: "#5A6270"
                                        font.pixelSize: 10
                                        font.family: "Consolas"
                                        elide: Text.ElideRight
                                        visible: modelData.payload.length > 0
                                    }
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: overlay.auditRows.length === 0
                                text: "（无记录）"
                                color: "#5A6270"
                                font.pixelSize: 13
                            }
                        }
                        Row {
                            spacing: 10
                            Rectangle {
                                width: 88; height: 32; radius: 8
                                color: pgPrevMa.containsMouse ? "#303744" : "#282E39"
                                border { color: "#39404E"; width: 1 }
                                opacity: overlay.auditPageIdx > 0 ? 1.0 : 0.4
                                Text { anchors.centerIn: parent; text: "← 上一页"; color: "#A7B0BF"; font.pixelSize: 12 }
                                MouseArea {
                                    id: pgPrevMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (overlay.auditPageIdx > 0) {
                                            overlay.auditPageIdx--;
                                            overlay.reloadAudit();
                                        }
                                    }
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "第 " + (overlay.auditPageIdx + 1) + " / " +
                                      Math.max(1, Math.ceil(overlay.auditTotal / overlay.auditPageSize)) +
                                      " 页 · 共 " + overlay.auditTotal + " 条（最新在前）"
                                color: "#707A8A"
                                font.pixelSize: 12
                            }
                            Rectangle {
                                width: 88; height: 32; radius: 8
                                color: pgNextMa.containsMouse ? "#303744" : "#282E39"
                                border { color: "#39404E"; width: 1 }
                                opacity: (overlay.auditPageIdx + 1) * overlay.auditPageSize < overlay.auditTotal ? 1.0 : 0.4
                                Text { anchors.centerIn: parent; text: "下一页 →"; color: "#A7B0BF"; font.pixelSize: 12 }
                                MouseArea {
                                    id: pgNextMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if ((overlay.auditPageIdx + 1) * overlay.auditPageSize < overlay.auditTotal) {
                                            overlay.auditPageIdx++;
                                            overlay.reloadAudit();
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                width: 64; height: 32; radius: 8
                                color: pgRefMa.containsMouse ? "#303744" : "#282E39"
                                border { color: "#39404E"; width: 1 }
                                Text { anchors.centerIn: parent; text: "刷新"; color: "#A7B0BF"; font.pixelSize: 12 }
                                MouseArea { id: pgRefMa; anchors.fill: parent; hoverEnabled: true; onClicked: overlay.reloadAudit() }
                            }
                        }
                    }
                }

                // ---- 审计导出（M4b · D22）----
                GroupBox {
                    width: parent.width
                    visible: overlay.unlocked
                    title: ""
                    padding: 14
                    background: Rectangle { color: "#21252C"; radius: 14; border.color: "#39404E"; border.width: 1.5 }
                    Column {
                        width: parent.width
                        spacing: 10
                        Text { text: "💾 审计导出（U盘 · CSV+JSON+SHA256 清单 · 导出留痕）"; color: "#A7B0BF"; font.pixelSize: 14; font.weight: Font.Black; font.letterSpacing: 1 }
                        Row {
                            spacing: 10
                            Rectangle {
                                width: 100; height: 34; radius: 8
                                color: expRefMa.containsMouse ? "#303744" : "#282E39"
                                border { color: "#39404E"; width: 1 }
                                Text { anchors.centerIn: parent; text: "刷新设备"; color: "#A7B0BF"; font.pixelSize: 12 }
                                MouseArea { id: expRefMa; anchors.fill: parent; hoverEnabled: true; onClicked: overlay.reloadExports() }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: overlay.exportDevs.length + " 个可写卷"
                                color: "#707A8A"
                                font.pixelSize: 12
                            }
                        }
                        Repeater {
                            model: overlay.exportDevs
                            Rectangle {
                                width: parent.width
                                height: 38
                                radius: 10
                                color: overlay.exportSel === index ? "#1D3A5C" : "#282E39"
                                border { color: overlay.exportSel === index ? "#4DA3FF" : "#39404E"; width: 1 }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: { overlay.exportSel = index; expDirIn.text = ""; }
                                }
                                Row {
                                    anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 12; right: parent.right; rightMargin: 12 }
                                    spacing: 8
                                    Text {
                                        text: (overlay.exportSel === index ? "● " : "○ ") + modelData.name + "  " + modelData.path
                                        color: overlay.exportSel === index ? "#4DA3FF" : "#ECF0F7"
                                        font.pixelSize: 13
                                        font.family: "Consolas"
                                        elide: Text.ElideMiddle
                                        width: parent.width - 110
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: "剩余 " + modelData.freeMB + " MB"
                                        color: "#707A8A"
                                        font.pixelSize: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }
                        TextField {
                            id: expDirIn
                            width: parent.width
                            placeholderText: "或输入目标目录路径（开发调试用；填写后优先于上方所选卷）"
                            color: "#ECF0F7"; font.pixelSize: 13; font.family: "Consolas"
                            background: Rectangle { color: "#4d000000"; radius: 10; border.color: expDirIn.activeFocus ? "#4DA3FF" : "#39404E" }
                            onTextChanged: {
                                if (expDirIn.text.length > 0) overlay.exportSel = -1;
                            }
                        }
                        Row {
                            spacing: 6
                            Repeater {
                                model: [["全量", 0], ["近30天", 1], ["近90天", 2]]
                                Rectangle {
                                    width: rngTxt.implicitWidth + 22
                                    height: 28
                                    radius: 8
                                    color: overlay.exportRange === modelData[1] ? "#4DA3FF" : "#282E39"
                                    border { color: "#39404E"; width: 1 }
                                    Text {
                                        id: rngTxt
                                        anchors.centerIn: parent
                                        text: modelData[0]
                                        color: overlay.exportRange === modelData[1] ? "#0B1220" : "#A7B0BF"
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: overlay.exportRange = modelData[1] }
                                }
                            }
                        }
                        Rectangle {
                            width: 160; height: 42; radius: 10
                            color: expGoMa.containsMouse ? "#00b956" : "#00A94F"
                            Text { anchors.centerIn: parent; text: "导出到目标"; color: "#fff"; font.pixelSize: 15; font.weight: Font.Black }
                            MouseArea {
                                id: expGoMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    var dir = expDirIn.text.trim();
                                    if (dir.length === 0 && overlay.exportSel >= 0 && overlay.exportSel < overlay.exportDevs.length)
                                        dir = overlay.exportDevs[overlay.exportSel].path;
                                    if (dir.length === 0) {
                                        overlay.exportStatus = "✘ 请先选择导出目标（U盘或目录）";
                                        return;
                                    }
                                    overlay.exportStatus = "导出中…";
                                    var r = App.exportAuditTo(dir, overlay.exportRange, 0);
                                    if (r.ok) {
                                        overlay.exportStatus = "✔ 导出 " + r.events + " 条事件 + " + r.snapshots +
                                                " 条快照\n文件：" + r.csvName + " · " + r.jsonName +
                                                "\n清单：" + r.manifest + "（SHA256 前缀 " + r.manifestSha + "…）" +
                                                "\n导出时链校验：" + (r.chainVerified ? "通过 ✔" : "未通过 ✘ 请检查审计库！");
                                        overlay.auditPageIdx = 0;
                                        overlay.reloadAudit();
                                    } else {
                                        overlay.exportStatus = "✘ " + r.error;
                                    }
                                }
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: overlay.exportStatus
                            visible: overlay.exportStatus.length > 0
                            color: overlay.exportStatus.indexOf("✔") === 0 ? "#22C55E" : (overlay.exportStatus.indexOf("✘") === 0 ? "#EF4444" : "#A7B0BF")
                            font.pixelSize: 12
                            font.family: "Consolas"
                            lineHeight: 1.3
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: "导出件可用 sha256sum 独立复核清单哈希；EXPORT 事件（含逐文件 SHA256）已写入设备审计库——导出件与库内记录互为凭证（D22）"
                            color: "#707A8A"
                            font.pixelSize: 11
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
        onAuditStateChanged: overlay.reloadAudit()   // 降级/恢复 → 状态灯与列表刷新
        onOperatorsChanged: overlay.reloadOps()
        onCurrentOperatorChanged: overlay.reloadOps()
    }
}
