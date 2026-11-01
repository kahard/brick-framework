#pragma once

// The page served by WebScreen. {{TITLE}} is replaced by the board name.
// Messages from the server (binary, little-endian, several per frame):
//   1 width:u16 height:u16                      screen size; clears to black
//   2 x:u16 y:u16 w:u16 h:u16 pixels:u16[w*h]   RGB565 rectangle
//   3 percent:u8                                backlight level
// Messages to the server (text): "d x y", "m x y", "u x y" (touch down, move,
// up in display pixels), "b 1" / "b 0" (button pressed / released).
namespace brick::platform::pc
{

inline constexpr char kWebScreenPage[] = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{{TITLE}}</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; min-height: 100vh; display: flex; flex-direction: column; align-items: center;
         justify-content: center; gap: 12px; background: #1e1f22; color: #e3e3e3;
         font: 14px system-ui, sans-serif; }
  h1 { margin: 0; font-size: 16px; font-weight: 600; }
  canvas { image-rendering: pixelated; touch-action: none; background: #000;
           box-shadow: 0 0 0 1px #4a4c50; cursor: crosshair; }
  .bar { display: flex; align-items: center; gap: 12px; }
  button { font: inherit; padding: 8px 20px; border-radius: 6px; border: 1px solid #5f6368;
           background: #2b2d31; color: inherit; touch-action: none; }
  button:active { background: #3c4043; }
  #status { opacity: 0.7; }
</style>
</head>
<body>
<h1>{{TITLE}}</h1>
<canvas id="screen" width="320" height="240"></canvas>
<div class="bar">
  <button id="button" title="Space">Button</button>
  <span id="status">connecting...</span>
</div>
<script>
"use strict";
const canvas = document.getElementById("screen");
const context = canvas.getContext("2d");
const status = document.getElementById("status");
let socket = null;

function connect() {
  socket = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws");
  socket.binaryType = "arraybuffer";
  socket.onopen = () => { status.textContent = "connected"; };
  socket.onclose = () => { status.textContent = "disconnected, retrying..."; setTimeout(connect, 1000); };
  socket.onmessage = (event) => { if (typeof event.data !== "string") draw(new DataView(event.data)); };
}

function draw(view) {
  let offset = 0;
  while (offset < view.byteLength) {
    const type = view.getUint8(offset++);
    if (type === 1) {
      canvas.width = view.getUint16(offset, true);
      canvas.height = view.getUint16(offset + 2, true);
      offset += 4;
      context.fillStyle = "#000";
      context.fillRect(0, 0, canvas.width, canvas.height);
      fit();
    } else if (type === 2) {
      const x = view.getUint16(offset, true), y = view.getUint16(offset + 2, true);
      const width = view.getUint16(offset + 4, true), height = view.getUint16(offset + 6, true);
      offset += 8;
      const image = context.createImageData(width, height);
      const rgba = image.data;
      for (let index = 0; index < width * height; index++, offset += 2) {
        const pixel = view.getUint16(offset, true);
        const red = pixel >> 11, green = (pixel >> 5) & 63, blue = pixel & 31;
        rgba[index * 4] = (red << 3) | (red >> 2);
        rgba[index * 4 + 1] = (green << 2) | (green >> 4);
        rgba[index * 4 + 2] = (blue << 3) | (blue >> 2);
        rgba[index * 4 + 3] = 255;
      }
      context.putImageData(image, x, y);
    } else if (type === 3) {
      canvas.style.filter = "brightness(" + view.getUint8(offset++) + "%)";
    } else {
      break;
    }
  }
}

// As large as the window allows: whole multiples of the display when enlarged,
// so pixels stay sharp; shrunk freely on a small screen.
function fit() {
  const room = Math.min(window.innerWidth * 0.96 / canvas.width, window.innerHeight * 0.8 / canvas.height);
  const scale = room >= 1 ? Math.floor(room) : room;
  canvas.style.width = Math.floor(canvas.width * scale) + "px";
  canvas.style.height = Math.floor(canvas.height * scale) + "px";
}
window.addEventListener("resize", fit);

function send(text) {
  if (socket && socket.readyState === WebSocket.OPEN) socket.send(text);
}

function position(event) {
  const box = canvas.getBoundingClientRect();
  const x = Math.floor((event.clientX - box.left) * canvas.width / box.width);
  const y = Math.floor((event.clientY - box.top) * canvas.height / box.height);
  return x + " " + y;
}

canvas.addEventListener("pointerdown", (event) => {
  canvas.setPointerCapture(event.pointerId);
  send("d " + position(event));
});
canvas.addEventListener("pointermove", (event) => { if (event.buttons) send("m " + position(event)); });
canvas.addEventListener("pointerup", (event) => send("u " + position(event)));
canvas.addEventListener("pointercancel", (event) => send("u " + position(event)));

const button = document.getElementById("button");
button.addEventListener("pointerdown", () => send("b 1"));
button.addEventListener("pointerup", () => send("b 0"));
button.addEventListener("pointerleave", () => send("b 0"));
document.addEventListener("keydown", (event) => {
  if (event.code === "Space" && !event.repeat) { send("b 1"); event.preventDefault(); }
});
document.addEventListener("keyup", (event) => { if (event.code === "Space") send("b 0"); });

connect();
</script>
</body>
</html>
)PAGE";

}  // namespace brick::platform::pc
