import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

Rectangle {
    id: root
    height: 80
    radius: 10
    color: Qt.rgba(255, 255, 255, 0.03)

    property string label: ""
    property string placeholder: ""
    property var validator: null
    property string tooltip: ""
    property alias value: input.text

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4

        Text {
            text: label
            color: "white"
            font.pixelSize: 14
            font.weight: Font.Medium
        }

        TextField {
            id: input
            Layout.fillWidth: true
            placeholderText: placeholder
            validator: root.validator
            color: "white"
            font.pixelSize: 16
            
            background: Rectangle {
                color: "transparent"
                border.color: parent.activeFocus ? Material.accent : Qt.rgba(255, 255, 255, 0.1)
                border.width: 2
                radius: 6
            }

            ToolTip.visible: hovered
            ToolTip.text: tooltip
        }
    }

    // Validation state
    Rectangle {
        width: 8
        height: 8
        radius: 4
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        color: {
            if (input.text === "") return "transparent"
            return input.acceptableInput ? "#5c5c5c" : "#F44336"
        }
    }
}