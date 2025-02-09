import QtQuick 2.0

QtObject {
    property string url

    property bool dataOK: false

    Component.onCompleted: {
        var x = new XMLHttpRequest;
        x.open("POST", url);
        x.setRequestHeader("Content-Type", "text/plain;charset=UTF-8");

        // Test to the end
        x.onreadystatechange = function() {
            if (x.readyState == XMLHttpRequest.DONE) {
                dataOK = (x.responseText == "QML Rocks!\n");
            }
        }

        x.send("My Sent Data\n");
    }
}

