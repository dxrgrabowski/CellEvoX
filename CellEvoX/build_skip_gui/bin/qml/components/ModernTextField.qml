import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 5

    property string label: ""
    property string placeholderText: ""
    property alias text: input.text
    property bool isValid: true

    Text {
        text: label
        color: "#ffffff"
        font.pixelSize: 14
    }

    TextField {
        id: input
        Layout.fillWidth: true
        placeholderText: root.placeholderText
        color: "#ffffff"
        background: Rectangle {
            color: "#ffffff15"
            radius: 5
            border.width: 1
            border.color: input.focused ? "#1DB954" : "#ffffff30"

            Behavior on border.color {
                ColorAnimation { duration: 200 }
            }
        }

        Rectangle {
            width: parent.width
            height: 2
            color: "#1DB954"
            opacity: input.focused ? 1 : 0
            anchors.bottom: parent.bottom

            Behavior on opacity {
                NumberAnimation { duration: 200 }
            }
        }
    }
}