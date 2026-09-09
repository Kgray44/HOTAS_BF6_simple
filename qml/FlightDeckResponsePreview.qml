import QtQuick 6.5

// Static, bounded configuration preview. The samples are rebuilt only when a
// user changes configuration or expands another axis; live axis telemetry does
// not participate in its bindings.
Item {
    id: root

    property QtObject tokens
    property int axisIndex: -1
    property int configurationRevision: 0
    property var transferEvaluator: null
    property bool unipolar: false
    property var samples: []

    implicitHeight: 210

    function rebuildSamples() {
        if (axisIndex < 0 || !transferEvaluator) {
            samples = [];
            return;
        }
        const domainMinimum = unipolar ? 0 : -1;
        const count = 33;
        const next = [];
        for (let index = 0; index < count; ++index) {
            const input = domainMinimum + (1 - domainMinimum) * index / (count - 1);
            next.push({
                input: input,
                output: transferEvaluator(axisIndex, input)
            });
        }
        samples = next;
        chart.requestPaint();
    }

    onAxisIndexChanged: rebuildSamples()
    onConfigurationRevisionChanged: rebuildSamples()
    onUnipolarChanged: rebuildSamples()
    Component.onCompleted: rebuildSamples()

    Canvas {
        id: chart
        anchors.fill: parent
        antialiasing: true
        renderTarget: Canvas.Image

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const context = getContext("2d");
            context.clearRect(0, 0, width, height);
            const left = 34;
            const top = 12;
            const right = 12;
            const bottom = 25;
            const plotWidth = Math.max(1, width - left - right);
            const plotHeight = Math.max(1, height - top - bottom);
            const domainMinimum = root.unipolar ? 0 : -1;
            const xFor = function (value) {
                return left + (value - domainMinimum) / (1 - domainMinimum) * plotWidth;
            };
            const yFor = function (value) {
                return top + (1 - (value - domainMinimum) / (1 - domainMinimum)) * plotHeight;
            };

            context.fillStyle = root.tokens.primarySurface;
            context.fillRect(left, top, plotWidth, plotHeight);
            context.strokeStyle = root.tokens.divider;
            context.lineWidth = 1;
            for (let tick = 0; tick <= 4; ++tick) {
                const x = left + plotWidth * tick / 4;
                const y = top + plotHeight * tick / 4;
                context.beginPath();
                context.moveTo(x, top);
                context.lineTo(x, top + plotHeight);
                context.stroke();
                context.beginPath();
                context.moveTo(left, y);
                context.lineTo(left + plotWidth, y);
                context.stroke();
            }
            if (!root.unipolar) {
                context.strokeStyle = root.tokens.textMuted;
                context.beginPath();
                context.moveTo(xFor(0), top);
                context.lineTo(xFor(0), top + plotHeight);
                context.stroke();
                context.beginPath();
                context.moveTo(left, yFor(0));
                context.lineTo(left + plotWidth, yFor(0));
                context.stroke();
            }

            context.strokeStyle = root.tokens.textMuted;
            context.lineWidth = 1;
            context.setLineDash([4, 4]);
            context.beginPath();
            context.moveTo(xFor(domainMinimum), yFor(domainMinimum));
            context.lineTo(xFor(1), yFor(1));
            context.stroke();
            context.setLineDash([]);
            if (root.samples.length > 0) {
                context.strokeStyle = root.tokens.accent;
                context.lineWidth = 2;
                context.beginPath();
                for (let index = 0; index < root.samples.length; ++index) {
                    const point = root.samples[index];
                    const x = xFor(Number(point.input));
                    const y = yFor(Number(point.output));
                    if (index === 0)
                        context.moveTo(x, y);
                    else
                        context.lineTo(x, y);
                }
                context.stroke();
            }
            context.fillStyle = root.tokens.textMuted;
            context.font = "9px " + root.tokens.telemetryFont;
            const labels = root.unipolar ? ["0", "25", "50", "75", "100"] : ["-100", "-50", "0", "+50", "+100"];
            for (let index = 0; index < labels.length; ++index) {
                context.fillText(labels[index], left + plotWidth * index / 4 - 10, height - 7);
            }
        }
    }

    Connections {
        target: root.tokens
        function onLightChanged() {
            chart.requestPaint();
        }
    }
}
