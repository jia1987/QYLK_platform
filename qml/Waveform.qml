// 频率-时间示意波形（原型 .ft svg 的 Canvas 移植）：
// mode 0=恒频(直线) 1=扫频(三角波) 2=阶频(双梯形循环)
import QtQuick 2.12

Item {
    id: wave
    property int mode: 0
    property color lineColor: "#4DA3FF"
    property bool dim: false

    onModeChanged: cv.requestPaint()
    onLineColorChanged: cv.requestPaint()
    onDimChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    onHeightChanged: cv.requestPaint()

    Canvas {
        id: cv
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            ctx.save();
            ctx.scale(width / 168, height / 80);

            var x0 = 26, x1 = 152, base = 66, top = 14, w = x1 - x0;

            // 坐标轴
            ctx.strokeStyle = wave.dim ? "#2A303C" : "#4A5262";
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(x0, top - 6); ctx.lineTo(x0, base);
            ctx.moveTo(x0, base); ctx.lineTo(x1 + 8, base);
            ctx.stroke();

            // 轴标签
            ctx.fillStyle = "#707A8A";
            ctx.font = "9px Consolas, monospace";
            ctx.fillText("f", 4, 14);
            ctx.fillText("t", x1 - 4, 78);

            // 曲线
            ctx.strokeStyle = wave.dim ? "#3A4150" : String(wave.lineColor);
            ctx.lineWidth = 3;
            ctx.lineJoin = "round";
            ctx.lineCap = "round";
            ctx.beginPath();
            if (wave.mode === 0) {
                ctx.moveTo(x0, top + 6);
                ctx.lineTo(x1, top + 6);
            } else if (wave.mode === 1) {
                var q = w / 4;
                ctx.moveTo(x0, base);
                ctx.lineTo(x0 + q, top);
                ctx.lineTo(x0 + 2 * q, base);
                ctx.lineTo(x0 + 3 * q, top);
                ctx.lineTo(x1, base);
            } else {
                var W = w / 2;
                var cyc = function (sx) {
                    ctx.lineTo(sx + 0.24 * W, top);
                    ctx.lineTo(sx + 0.50 * W, top);
                    ctx.lineTo(sx + 0.76 * W, base);
                    ctx.lineTo(sx + W, base);
                };
                ctx.moveTo(x0, base);
                cyc(x0);
                cyc(x0 + W);
            }
            ctx.stroke();
            ctx.restore();
        }
    }
}
