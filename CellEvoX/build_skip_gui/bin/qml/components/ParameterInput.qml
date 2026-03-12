import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import QtQuick.Effects

Rectangle {
    id: root
    color: "transparent"
    radius: 10
    
    property var parameters: [
        {label: "N", value: "0", tooltip: "Population size"},
        {label: "Nc", value: "0", tooltip: "Critical population"},
        {label: "Effect", value: "0", tooltip: "Effect strength"},
        {label: "Prob", value: "0", tooltip: "Probability"},
        {label: "TauStep", value: "0", tooltip: "Time step"},
        {label: "nP", value: "0", tooltip: "Parameter n"},
        {label: "nT", value: "0", tooltip: "Parameter t"}
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Repeater {
            model: parameters
            delegate: Rectangle {
                Layout.fillWidth: true
                height: 60
                color: "#333333"
                radius: 8

                MultiEffect {
                    source: parent
                    blur: 0.3
                    brightness: 0.05
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    Text {
                        text: modelData.label
                        color: "#ffffff"
                        font.pixelSize: 16
                    }

                    TextField {
                        Layout.fillWidth: true
                        text: modelData.value
                        color: "#ffffff"
                        background: Rectangle {
                            color: "#404040"
                            radius: 4
                        }
                        
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.tooltip
                    }
                }
            }
        }
    }
}