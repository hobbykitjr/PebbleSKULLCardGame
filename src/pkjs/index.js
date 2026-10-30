/**
 * Skull - PebbleKit JS
 * Handles settings config page
 */

var settings = {
  touchMode: 1
};

function loadSettings() {
  try {
    var s = localStorage.getItem('skull_settings');
    if(s) {
      var p = JSON.parse(s);
      if(p.touchMode !== undefined) settings.touchMode = p.touchMode;
    }
  } catch(e) {}
}

function saveSettings() {
  localStorage.setItem('skull_settings', JSON.stringify(settings));
}

Pebble.addEventListener('showConfiguration', function() {
  var url = 'https://hobbykitjr.github.io/PebbleSKULLCardGame/config/index.html' +
    '?touch=' + settings.touchMode;
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if(e && e.response && e.response.length > 0) {
    try {
      var decoded;
      try { decoded = decodeURIComponent(e.response); } catch(de) { decoded = e.response; }
      var config = JSON.parse(decoded);
      if(config.touchMode !== undefined) settings.touchMode = parseInt(config.touchMode);
      saveSettings();
      Pebble.sendAppMessage({'TOUCH_MODE': settings.touchMode},
        function() { console.log('Settings sent'); },
        function() { console.log('Settings send failed'); });
    } catch(err) {
      console.log('Config parse error: ' + err);
    }
  }
});

Pebble.addEventListener('ready', function() {
  console.log('Skull JS ready');
  loadSettings();
  Pebble.sendAppMessage({'TOUCH_MODE': settings.touchMode},
    function() { console.log('Init settings sent'); },
    function() { console.log('Init settings send failed'); });
});
