// FileOperations.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs 6.3

Rectangle {
    id: fileCard
    height: 120
    color: cardColor
    radius: root.radius

    FileDialog {
        id: fileDialog
        title: "Load Parameters"
        nameFilters: ["Parameter files (*.json *.txt)"]
        onAccepted: {
            // Handle file loading
            console.log("Selected file:", fileUrl)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            text: "File Operations"
            color: textColor
            font.pixelSize: 18
            font.weight: Font.Medium
        }

        RowLayout {
            spacing: 12

            Button {
                text: "Load Parameters"
                icon.source: "qrc:/icons/load.svg"
                
                background: Rectangle {
                    color: parent.down ? Qt.darker(accentColor, 1.2) : accentColor
                    radius: 6
                }

                onClicked: fileDialog.open()

                Behavior on scale {
                    NumberAnimation {
                        duration: 100
                    }
                }
            }

            Button {
                text: "Save Configuration"
                icon.source: "qrc:/icons/save.svg"
                
                background: Rectangle {
                    color: parent.down ? Qt.darker(accentColor, 1.2) : accentColor
                    radius: 6
                }

                onClicked: {
                    // Handle save functionality
                }
            }
        }
    }
}