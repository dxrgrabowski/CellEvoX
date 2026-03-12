// main.qml
import QtQuick 
import QtQuick.Controls 
import QtQuick.Window 
import QtQuick.Dialogs 
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import CellEvoX 1.0
import "components" 
import QtQuick.Controls.Material
import QtQuick.Effects
//import Qt.labs.platform 1.1 as Platform

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1280
    height: 800
    title: "Scientific Simulation Interface"
    color: "#1a1a1a"  // Dark theme base color

    Rectangle {
        id: mainContainer
        anchors.fill: parent
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            spacing: 20
            
            // Left panel for parameters
            Rectangle {
                Layout.preferredWidth: parent.width * 0.3
                Layout.fillHeight: true
                color: "#2a2a2a"
                radius: 15

                MultiEffect {
                    source: parent
                    blur: 0.5
                    brightness: 0.1
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 15

                    Text {
                        text: "Simulation Parameters"
                        color: "#ffffff"
                        font.pixelSize: 24
                        font.weight: Font.Medium
                    }

                    ParameterInput {
                        Layout.fillWidth: true
                    }
                    
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                    ModelSwitch {
                        Layout.fillWidth: true
                    }

                    ModernButton {
                        id: startButton
                        Layout.fillWidth: true
                        buttonText: "Start Simulation"
                    }

                    ModernButton {
                        id: importButton
                        Layout.fillWidth: true
                        buttonText: "Import Parameters"
                    }

                    ModernButton {
                        id: exportButton
                        Layout.fillWidth: true
                        buttonText: "Export Parameters"
                    }
                }
            }

            // Right panel for visualization
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#2a2a2a"
                radius: 15

                MultiEffect {
                    source: parent
                    blur: 0.5
                    brightness: 0.1
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 15

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#333333"
                        radius: 10
                        // Graph placeholder
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}