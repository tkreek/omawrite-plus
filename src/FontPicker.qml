import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Popup {
    id: picker

    property var fonts: []
    property string currentFont
    property string defaultFont
    property bool darkMode: true
    property real textScale: 1
    property color textColor: "#d0d0d0"
    property color mutedColor: "#909191"
    property color highlightColor: "#428bca"
    property real maximumHeight: 440
    property int fontSize: 20
    property bool followsSystemSize: true

    signal fontChosen(string family)
    signal fontSizeStepped(int direction)
    signal systemSizeChosen()

    readonly property var filteredFonts: {
        var query = filterField.text.trim().toLocaleLowerCase();
        if (query.length === 0)
            return fonts;
        return fonts.filter(function(family) {
            return family.toLocaleLowerCase().indexOf(query) !== -1;
        });
    }

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    function choose(family) {
        if (family)
            fontChosen(family);
        close();
    }

    // Shrink to fit when a filter leaves only a few fonts.
    readonly property real headerHeight: scaledSize(34) * 2 + 26
    height: Math.min(maximumHeight, topPadding + bottomPadding + headerHeight
                     + Math.max(scaledSize(36), fontList.contentHeight))

    modal: false
    focus: true
    padding: 8
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.elevation: 8

    onAboutToShow: {
        filterField.text = "";
        fontList.currentIndex = Math.max(0, fonts.indexOf(currentFont));
        fontList.positionViewAtIndex(fontList.currentIndex, ListView.Center);
        filterField.forceActiveFocus();
    }

    background: Rectangle {
        radius: 9
        color: picker.darkMode ? "#22221f" : "#fffef2"
    }

    component SizeButton: Label {
        id: sizeButton
        property bool enabledState: true
        signal activated()

        width: picker.scaledSize(28)
        height: picker.scaledSize(28)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: picker.textColor
        opacity: enabledState ? 1 : 0.35
        font.family: picker.defaultFont
        font.pixelSize: picker.scaledSize(16)

        background: Rectangle {
            radius: 5
            color: picker.highlightColor
            opacity: sizeArea.containsMouse && sizeButton.enabledState ? 0.18 : 0
        }

        MouseArea {
            id: sizeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: if (sizeButton.enabledState) sizeButton.activated()
        }
    }

    contentItem: Column {
        spacing: 6

        Item {
            objectName: "fontSizeRow"
            width: parent.width
            height: picker.scaledSize(34)

            Label {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "Size"
                color: picker.mutedColor
                font.family: picker.defaultFont
                font.pixelSize: picker.scaledSize(14)
            }

            Row {
                anchors.right: systemSize.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                SizeButton {
                    objectName: "smallerButton"
                    text: "\u2212"
                    enabledState: picker.fontSize > 10
                    onActivated: picker.fontSizeStepped(-1)
                }

                Label {
                    width: picker.scaledSize(44)
                    height: picker.scaledSize(28)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: picker.fontSize + "px"
                    color: picker.textColor
                    font.family: picker.defaultFont
                    font.pixelSize: picker.scaledSize(14)
                }

                SizeButton {
                    objectName: "largerButton"
                    text: "+"
                    enabledState: picker.fontSize < 48
                    onActivated: picker.fontSizeStepped(1)
                }
            }

            SizeButton {
                id: systemSize
                objectName: "systemSizeButton"
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: implicitWidth + 12
                text: picker.followsSystemSize ? "\u2713 System" : "System"
                color: picker.followsSystemSize ? picker.textColor : picker.mutedColor
                font.pixelSize: picker.scaledSize(12)
                enabledState: true
                onActivated: picker.systemSizeChosen()
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: picker.mutedColor
            opacity: 0.3
        }

        Item {
            width: parent.width
            height: picker.scaledSize(34)

            TextInput {
                id: filterField
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                verticalAlignment: TextInput.AlignVCenter
                color: picker.textColor
                selectByMouse: true
                clip: true
                font.family: picker.defaultFont
                font.pixelSize: picker.scaledSize(14)
                onTextChanged: fontList.currentIndex = 0

                Keys.onUpPressed: fontList.decrementCurrentIndex()
                Keys.onDownPressed: fontList.incrementCurrentIndex()
                Keys.onReturnPressed: picker.choose(picker.filteredFonts[fontList.currentIndex])
                Keys.onEnterPressed: picker.choose(picker.filteredFonts[fontList.currentIndex])
            }

            Label {
                anchors.left: filterField.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Filter fonts"
                visible: filterField.text.length === 0
                color: picker.mutedColor
                font: filterField.font
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: picker.mutedColor
            opacity: 0.3
        }

        ListView {
            id: fontList
            objectName: "fontList"
            width: parent.width
            height: picker.availableHeight - picker.headerHeight
            clip: true
            model: picker.filteredFonts
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: ItemDelegate {
                id: row
                required property string modelData
                required property int index

                width: fontList.width
                height: picker.scaledSize(36)
                leftPadding: 8
                rightPadding: 8
                highlighted: ListView.isCurrentItem
                onClicked: picker.choose(modelData)

                background: Rectangle {
                    radius: 5
                    color: picker.highlightColor
                    opacity: row.highlighted ? 0.22 : (row.hovered ? 0.12 : 0)
                }

                contentItem: Item {
                    Label {
                        anchors.left: parent.left
                        anchors.right: badge.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData
                        elide: Text.ElideRight
                        color: picker.textColor
                        font.family: row.modelData
                        font.pixelSize: picker.scaledSize(16)
                    }

                    Label {
                        id: badge
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: [row.modelData === picker.currentFont ? "✓" : "",
                               row.modelData === picker.defaultFont ? "Default" : ""]
                              .filter(function(part) { return part.length > 0; }).join(" ")
                        color: picker.mutedColor
                        font.family: picker.defaultFont
                        font.pixelSize: picker.scaledSize(11)
                    }
                }
            }
        }
    }
}
