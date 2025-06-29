const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Camera Stream</title>
  <style>
    body {
      margin: 0; padding: 0;
      background-color: #000;
      text-align: center;
      color: #fff;
      font-family: sans-serif;
    }
    #stream {
      width: 100vw;
      max-width: 640px;
      margin-top: 20px;
      border: 2px solid #333;
    }
    h2 {
      margin-top: 20px;
      font-weight: normal;
    }
  </style>
</head>
<body>
  <h2>ESP32 Live Stream made by Dung Dap Chai</h2>
  <img id="stream" src="/stream" onerror="handleError()" />

  <script>
    function handleError() {
      document.getElementById("stream").src = "";
      const msg = document.createElement("p");
      msg.innerText = "Disconnected to camera!!!!";
      msg.style.color = "#f66";
      document.body.appendChild(msg);
    }
  </script>
</body>
</html>
)rawliteral";
