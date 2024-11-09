import QtQuick
import QtQuick.Controls
import QtQuick.Effects


Rectangle {
    id: root
    height: 60
    color: "#333333"
    radius: 10

    property bool isStochastic: false

    MultiEffect {
        source: parent
        blur: 0.3
        brightness: 0.05
    }

    Rectangle {
        id: switchTrack
        width: 120
        height: 40
        radius: height / 2
        anchors.centerIn: parent
        color: isStochastic ? "#404080" : "#804040"

        Rectangle {
            id: thumb
            width: parent.height - 8
            height: width
            radius: width / 2
            x: isStochastic ? parent.width - width - 4 : 4
            anchors.verticalCenter: parent.verticalCenter
            color: "#ffffff"

            Behavior on x {
                NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
            }
        }

        Row {
            anchors.fill: parent
            Text {
                width: parent.width / 2
                height: parent.height
                text: "RK4"
                color: "white"
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                width: parent.width / 2
                height: parent.height
                text: "Stochastic"
                color: "white"
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: isStochastic = !isStochastic
    }
}
