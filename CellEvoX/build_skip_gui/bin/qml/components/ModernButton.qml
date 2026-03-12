import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Rectangle {
    id: root
    height: 50
    radius: 10
    property string buttonText: "Button"
    property bool isPressed: false

    gradient: Gradient {
        GradientStop { position: 0.0; color: root.isPressed ? "#303030" : "#404040" }
        GradientStop { position: 1.0; color: root.isPressed ? "#202020" : "#303030" }
    }

    MultiEffect {
        source: parent
        blur: 0.3
        brightness: mouseArea.containsMouse ? 0.1 : 0.05
        Behavior on brightness {
            NumberAnimation { duration: 150 }
        }
    }

    Text {
        anchors.centerIn: parent
        text: root.buttonText
        color: "#ffffff"
        font.pixelSize: 16
        font.weight: Font.Medium
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onPressed: root.isPressed = true
        onReleased: root.isPressed = false
        onExited: root.isPressed = false
    }

    Behavior on scale {
        NumberAnimation {
            duration: 100
            easing.type: Easing.InOutQuad
        }
    }
}
