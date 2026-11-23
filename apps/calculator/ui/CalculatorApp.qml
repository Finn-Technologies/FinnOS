import QtQuick
import FinnUI 1.0

// Finn Calculator — KCalc-grade Standard & Scientific calculator with frosted
// sidebar mode switcher, memory register, keyboard entry, and expression history.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 680
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 540
    appTitle: "Finn Calculator"
    pageTitle: activeNavId === "scientific" ? "Scientific" : (activeNavId === "history" ? "History" : "Standard")
    activeNavId: "standard"
    showSidebarSearch: false

    navItems: [
        { id: "standard", label: "Standard", icon: "grid", badgeColor: "#007BFF", section: "mode" },
        { id: "scientific", label: "Scientific", icon: "code-01", badgeColor: "#7C3AED", section: "mode" },
        { id: "history", label: "History", icon: "activity", badgeColor: "#F59E0B", section: "tape" }
    ]

    property string display: "0"
    property string exprText: ""
    property real accumulator: 0
    property string pendingOp: ""
    property bool resetNext: true
    property real memoryVal: 0
    property var historyList: []

    function fmtNum(v) {
        if (!isFinite(v)) return "Error"
        return Number(v.toPrecision(12)).toString()
    }

    function pressDigit(d) {
        if (resetNext || display === "Error") {
            display = (d === ".") ? "0." : d
            resetNext = false
        } else {
            if (d === "." && display.indexOf(".") >= 0) return
            display = (display === "0" && d !== ".") ? d : (display + d)
        }
    }

    function applyOp(op) {
        const cur = parseFloat(display)
        if (!resetNext && pendingOp.length > 0) {
            evaluate()
        } else {
            accumulator = isNaN(cur) ? 0 : cur
        }
        pendingOp = op
        exprText = fmtNum(accumulator) + " " + op
        resetNext = true
    }

    function evaluate() {
        if (pendingOp.length === 0) return
        const cur = parseFloat(display)
        let res = accumulator
        if (pendingOp === "+") res = accumulator + cur
        else if (pendingOp === "−") res = accumulator - cur
        else if (pendingOp === "×") res = accumulator * cur
        else if (pendingOp === "÷") res = (cur === 0) ? NaN : (accumulator / cur)
        else if (pendingOp === "^") res = Math.pow(accumulator, cur)
        const line = fmtNum(accumulator) + " " + pendingOp + " " + fmtNum(cur) + " = " + fmtNum(res)
        historyList = [line].concat(historyList.slice(0, 29))
        exprText = fmtNum(accumulator) + " " + pendingOp + " " + fmtNum(cur) + " ="
        display = fmtNum(res)
        accumulator = res
        pendingOp = ""
        resetNext = true
    }

    function applyUnary(fn) {
        const cur = parseFloat(display)
        let res = cur
        if (fn === "±") res = -cur
        else if (fn === "%") res = cur / 100.0
        else if (fn === "√") res = Math.sqrt(cur)
        else if (fn === "x²") res = cur * cur
        else if (fn === "1/x") res = (cur === 0) ? NaN : (1.0 / cur)
        else if (fn === "sin") res = Math.sin(cur * Math.PI / 180.0)
        else if (fn === "cos") res = Math.cos(cur * Math.PI / 180.0)
        else if (fn === "tan") res = Math.tan(cur * Math.PI / 180.0)
        else if (fn === "ln") res = Math.log(cur)
        else if (fn === "log") res = Math.log10(cur)
        else if (fn === "π") res = Math.PI
        else if (fn === "e") res = Math.E
        display = fmtNum(res)
        resetNext = true
    }

    function clearAll() {
        display = "0"
        exprText = ""
        accumulator = 0
        pendingOp = ""
        resetNext = true
    }

    function backspace() {
        if (resetNext || display.length <= 1) {
            display = "0"
            resetNext = true
        } else {
            display = display.slice(0, -1)
        }
    }

    headerActions: [
        FinnBadge { text: "M: " + win.fmtNum(win.memoryVal); tone: win.memoryVal !== 0 ? "accent" : "neutral" },
        FinnButton { text: "MC"; implicitHeight: 26; onClicked: win.memoryVal = 0 },
        FinnButton { text: "MR"; implicitHeight: 26; onClicked: { win.display = win.fmtNum(win.memoryVal); win.resetNext = true } },
        FinnButton { text: "M+"; implicitHeight: 26; onClicked: win.memoryVal += parseFloat(win.display) || 0 }
    ]

    // Standard & Scientific Keypad View
    Column {
        visible: win.activeNavId !== "history"
        anchors.fill: parent
        spacing: 12
        focus: true

        Keys.onPressed: (ev) => {
            if (ev.text >= "0" && ev.text <= "9") win.pressDigit(ev.text)
            else if (ev.text === ".") win.pressDigit(".")
            else if (ev.text === "+") win.applyOp("+")
            else if (ev.text === "-") win.applyOp("−")
            else if (ev.text === "*") win.applyOp("×")
            else if (ev.text === "/") win.applyOp("÷")
            else if (ev.key === Qt.Key_Return || ev.key === Qt.Key_Enter || ev.text === "=") win.evaluate()
            else if (ev.key === Qt.Key_Backspace) win.backspace()
            else if (ev.key === Qt.Key_Escape) win.clearAll()
        }

        // Readout Card
        FinnCard {
            width: parent.width
            height: 108
            radius: FinnTheme.radiusLg

            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 4

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignRight
                    text: win.exprText
                    color: FinnTheme.mute
                    font.family: FinnTheme.fontMono
                    font.pixelSize: 13
                    elide: Text.ElideLeft
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignRight
                    text: win.display
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 38
                    font.weight: Font.Medium
                    elide: Text.ElideLeft
                }
            }
        }

        // Scientific Function Strip
        Grid {
            visible: win.activeNavId === "scientific"
            width: parent.width
            columns: 5
            columnSpacing: 8
            rowSpacing: 8
            readonly property real bw: (width - 32) / 5

            Repeater {
                model: ["sin", "cos", "tan", "ln", "log", "√", "x²", "1/x", "π", "e"]
                delegate: FinnButton {
                    width: parent.bw
                    implicitHeight: 36
                    text: modelData
                    onClicked: win.applyUnary(modelData)
                }
            }
        }

        // Core 4x5 Keypad
        Grid {
            width: parent.width
            height: parent.height - 120 - (win.activeNavId === "scientific" ? 88 : 0)
            columns: 4
            columnSpacing: 8
            rowSpacing: 8
            readonly property real bw: (width - 24) / 4
            readonly property real bh: Math.max(42, (height - 32) / 5)

            Repeater {
                model: [
                    { t: "C", kind: "clear" }, { t: "⌫", kind: "bs" }, { t: "%", kind: "unary" }, { t: "÷", kind: "op" },
                    { t: "7", kind: "num" },   { t: "8", kind: "num" }, { t: "9", kind: "num" },   { t: "×", kind: "op" },
                    { t: "4", kind: "num" },   { t: "5", kind: "num" }, { t: "6", kind: "num" },   { t: "−", kind: "op" },
                    { t: "1", kind: "num" },   { t: "2", kind: "num" }, { t: "3", kind: "num" },   { t: "+", kind: "op" },
                    { t: "±", kind: "unary" }, { t: "0", kind: "num" }, { t: ".", kind: "num" },   { t: "=", kind: "eq" }
                ]
                delegate: FinnButton {
                    width: parent.bw
                    implicitHeight: parent.bh
                    text: modelData.t
                    variant: modelData.kind === "eq" ? "primary" : "secondary"
                    onClicked: {
                        if (modelData.kind === "num") win.pressDigit(modelData.t)
                        else if (modelData.kind === "op") win.applyOp(modelData.t)
                        else if (modelData.kind === "unary") win.applyUnary(modelData.t)
                        else if (modelData.kind === "clear") win.clearAll()
                        else if (modelData.kind === "bs") win.backspace()
                        else if (modelData.kind === "eq") win.evaluate()
                    }
                }
            }
        }
    }

    // Calculation History Tape
    FinnCard {
        visible: win.activeNavId === "history"
        anchors.fill: parent
        radius: FinnTheme.radiusLg

        ListView {
            visible: win.historyList.length > 0
            anchors.fill: parent
            anchors.margins: 10
            model: win.historyList
            spacing: 4
            clip: true

            delegate: FinnSquircle {
                width: ListView.view.width
                height: 36
                radius: 8
                color: FinnTheme.surfaceCanvas
                borderWidth: 0

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    text: modelData
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontMono
                    font.pixelSize: 13
                }
            }
        }

        FinnEmptyState {
            visible: win.historyList.length === 0
            anchors.fill: parent
            iconName: "activity"
            headline: "No calculations yet"
            description: "Completed expressions will appear here."
        }
    }
}
