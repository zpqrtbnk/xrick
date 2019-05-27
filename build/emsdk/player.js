//console.log('begin player.js');

function getArgs() {
    var qs = window.location.search.substr(1);
    var args = [];
    if (qs == '')
        return args;
    var s = qs.split('&');
    for (var i = 0; i < s.length; i++) {
        var p = s[i].split('=', 2);
        args.push('-' + p[0]);
        if (p.length == 2)
            args.push(decodeURIComponent(p[1].replace(/\+/g, " ")));
    }
    return args;
}

function setArg(args, key, value) {
    key = '-' + key;
    if (!args.includes(key)) {
        args.push(key);
        args.push(value);
    }
}

var Module = {
    preRun: [],
    postRun: [ setClickStart ],
    print: (function () {
        var element = document.getElementById('output');
        if (element) element.value = ''; // clear browser cache
        return function (text) {
            if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
            // These replacements are necessary if you render to raw HTML
            //text = text.replace(/&/g, "&amp;");
            //text = text.replace(/</g, "&lt;");
            //text = text.replace(/>/g, "&gt;");
            //text = text.replace('\n', '<br>', 'g');
            console.log(text);
            if (element) {
                element.value += text + "\n";
                element.scrollTop = element.scrollHeight; // focus on bottom
            }
        };
    })(),
    printErr: function (text) {
        if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
        console.error(text);
    },
    canvas: (function () {
        var canvas = document.getElementById('canvas');

        // As a default initial behavior, pop up an alert when webgl context is lost. To make your
        // application robust, you may want to override this behavior before shipping!
        // See http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
        canvas.addEventListener("webglcontextlost", function (e) { alert('WebGL context lost. You will need to reload the page.'); e.preventDefault(); }, false);

        return canvas;
    })(),
    canSpan: true,
    setStatus: function (text) {
        if (!Module.setStatus.last) Module.setStatus.last = { time: Date.now(), text: '' };
        if (text === Module.setStatus.last.text) return;
        var m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
        var now = Date.now();
        if (m && now - Module.setStatus.last.time < 30) return; // if this is a progress update, skip it if too soon
        Module.setStatus.last.time = now;
        Module.setStatus.last.text = text;

        if (m) {
            text = m[1];
        } else {
            if (!text) {
                // fixme - stop doing this and use a postRun
                this.canSpan = false;
                //setClickStart();
            }
        }
        if (this.canSpan)
            $('#xrick span').html(text + (m ? (parseInt(m[2]) * 100) + " / " + (parseInt(m[4]) * 100) : ""));
    },
    totalDependencies: 0,
    monitorRunDependencies: function (left) {
        this.totalDependencies = Math.max(this.totalDependencies, left);
        Module.setStatus(left ? 'Preparing... (' + (this.totalDependencies - left) + '/' + this.totalDependencies + ')' : 'All downloads complete.');
    }
};

window.onerror = function (event) {
    // TODO: do not warn on ok events like simulating an infinite loop or exitStatus
    console.error(event.toString());
    Module.setStatus('Exception thrown, see JavaScript console');
    Module.setStatus = function (text) {
        if (text) Module.printErr('[post-exception status] ' + text);
    };
};

function console2div() {
    $('#player_console').show();
    //console.debug =
    console.error = console.info =
        console.log = (function (oldFunction, divLog) {
            return function (text) {
                oldFunction(text);
                var txt = document.createTextNode(text);
                var div = document.createElement('div');
                div.appendChild(txt);
                divLog.appendChild(div);
            };

        }(console.log.bind(console), document.getElementById("player_console")));
}

function startAudio() {
    var audio = $('<audio></audio>');
    var source = $('<source></source>');
    source.attr('src', "data:audio/wave;base64,UklGRjIAAABXQVZFZm10IBIAAAABAAEAQB8AAEAfAAABAAgAAABmYWN0BAAAAAAAAABkYXRhAAAAAA==");
    source.appendTo(audio);
    audio.appendTo('body');
    console.log(audio);
    //audio[0].play();
    window.document.getElementsByTagName('audio')[0].play(); // meh
}

function isIPx() {
    return (navigator.platform.indexOf("iPhone") != -1)
    || (navigator.platform.indexOf("iPad") != -1)
    || (navigator.platform.indexOf("iPod") != -1);
}

function setClickStart() {
    $('#xrick span').html('click to play');
    $('#xrick').on('click', function () {
        
        // unlock iPhone/iPad audio
        if (isIPx())
            startAudio();

        // switch to canvas
        $('#xrick').hide();
        $('#canvas').css('display', 'block'); // show() would use 'inline' on a canvas
        $('#canvas').on('blur', function() { console.log('pause'); });

        // run
        Module['callMain'](Module['arguments']);
    });
}

// get arguments from querystring, and add missing arguments
var args = getArgs();
setArg(args, 'data', '/data');
setArg(args, 'keys', 'W-X-O-K-SPACE');
setArg(args, 'zoom', '1');
Module['arguments'] = args;

Module['locateFile'] = function (path, prefix) {
    // if it's the data file, use cdn
    if (path.endsWith(".data")) return "http://www1.xrick.net/" + path;
    // otherwise, use the default, the prefix (JS file's dir) + the path
    return prefix + path;
}

// no initial run!
Module['noInitialRun'] = true;

Module.setStatus('Downloading...');

//console.log('end player.js');