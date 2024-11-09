import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Rectangle {
    id: root
    color: "#2d2d2d"
    radius: 10

    MultiEffect {
        source: parent
        blur: 0.3
        brightness: 0.05
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 10

        ComboBox {
            id: graphTypeSelector
            Layout.fillWidth: true
            model: ["Time Series", "Phase Portrait", "Distribution"]
            background: Rectangle {
                color: "#404040"
                radius: 5
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#333333"
            radius: 8

            Text {
                anchors.centerIn: parent
                text: "Graph Visualization Area"
                color: "#808080"
                font.pixelSize: 18
            }
        }
    }
}
