import QtQuick
import QtQuick.Controls

Button {
    id: control
    
    contentItem: Text {
        text: control.text
        color: "#ffffff"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        color: control.pressed ? "#1DB954" : "#1DB95480"
        radius: 5
        
        Rectangle {
            anchors.fill: parent
            color: "#ffffff"
            radius: parent.radius
            opacity: control.hovered ? 0.1 : 0
            
            Behavior on opacity {
                NumberAnimation { duration: 200 }
            }
        }
    }
}