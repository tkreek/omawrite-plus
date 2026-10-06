import QtQuick
import QtMultimedia

// Plays a typewriter sound for each key the editor receives. The effects are
// only loaded while sounds are switched on, so the audio device stays closed
// otherwise.
Item {
    id: sounds

    property bool active: false
    property real volume: 0.35

    component Effect: SoundEffect {
        volume: sounds.volume
    }

    function keyPressed(event) {
        if (!active || event.isAutoRepeat || !player.item)
            return;

        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return;

        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            player.item.play(player.item.returns);
        else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete)
            player.item.play(player.item.backspaces);
        else if (event.key === Qt.Key_Space)
            player.item.play(player.item.spaces);
        else if (event.text.length > 0)
            player.item.play(player.item.keys);
    }

    Loader {
        id: player
        active: sounds.active
        sourceComponent: Item {
            // Several effects per kind, because replaying a SoundEffect that
            // is still sounding cuts it off. Each key is a different stroke
            // from the same Selectric, so fast typing never sounds looped.
            readonly property var keys: [key1, key2, key3, key4, key5, key6, key7, key8]
            readonly property var spaces: [space1, space2]
            readonly property var backspaces: [backspaceA, backspaceB]
            readonly property var returns: [returnA]
            property var nextIndex: ({})

            function play(effects) {
                var name = effects[0].objectName;
                var index = nextIndex[name] || 0;
                if (effects === keys)
                    index = (index + 1 + Math.floor(Math.random() * 3)) % effects.length;
                else
                    index = (index + 1) % effects.length;
                nextIndex[name] = index;
                effects[index].play();
            }

            Effect { id: key1; objectName: "key"; source: "qrc:/sounds/key1.wav" }
            Effect { id: key2; source: "qrc:/sounds/key2.wav" }
            Effect { id: key3; source: "qrc:/sounds/key3.wav" }
            Effect { id: key4; source: "qrc:/sounds/key4.wav" }
            Effect { id: key5; source: "qrc:/sounds/key5.wav" }
            Effect { id: key6; source: "qrc:/sounds/key6.wav" }
            Effect { id: key7; source: "qrc:/sounds/key7.wav" }
            Effect { id: key8; source: "qrc:/sounds/key8.wav" }
            Effect { id: space1; objectName: "space"; source: "qrc:/sounds/space1.wav" }
            Effect { id: space2; source: "qrc:/sounds/space2.wav" }
            Effect { id: backspaceA; objectName: "backspace"; source: "qrc:/sounds/backspace.wav" }
            Effect { id: backspaceB; source: "qrc:/sounds/backspace.wav" }
            Effect { id: returnA; objectName: "return"; source: "qrc:/sounds/return.wav" }
        }
    }
}
