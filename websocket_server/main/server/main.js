document.getElementById("state").innerHTML = "WebSocket is not connected";

let websocket = new WebSocket("ws://" + location.hostname + "/");
// let slider = document.getElementById("myRange");

// slider.oninput = function () {
//   websocket.send("L" + slider.value);
// };

// function sendMsg() {
//   websocket.send("L50");
//   console.log("Sent message to websocket");
// }

// function sendText(text) {
//   websocket.send("M" + text);
// }

websocket.onopen = function (evt) {
  // console.log("WebSocket connection opened");
  // websocket.send("It's open! Hooray!!!");
  document.getElementById("state").innerHTML = "WebSocket is connected";
};

// websocket.onmessage = function (evt) {
//   let msg = evt.data;
//   let value;
//   switch (msg.charAt(0)) {
//     case "L":
//       console.log(msg);
//       value = parseInt(msg.replace(/[^0-9\.]/g, ""), 10);
//       slider.value = value;
//       console.log("Led = " + value);
//       break;
//     default:
//       document.getElementById("output").innerHTML = evt.data;
//       break;
//   }
// };

websocket.onclose = function (evt) {
  // console.log("Websocket connection closed");
  document.getElementById("state").innerHTML = "WebSocket closed";
};

websocket.onerror = function (evt) {
  // console.log("Websocket error: " + evt);
  document.getElementById("state").innerHTML = "WebSocket error!";
};

let onMouseDown = false;
const joystick = () => {
  const container = document.getElementById("joystick");
  const width = container.clientWidth;
  const height = container.clientHeight;
  const radius = width * 0.4 < height * 0.8 ? width * 0.4 : height * 0.8;
  const x_origin = width / 2;
  const y_origin = height - (height - radius) / 2;

  const stick = () => {
    const canvas = document.getElementById("stick");
    const context = canvas.getContext("2d");
    const angle = (x, y) => {
      return Math.atan2(y - y_origin, x - x_origin);
    };

    const speed = (x, y) => {
      return Math.round(
        (100 *
          Math.sqrt(Math.pow(x - x_origin, 2) + Math.pow(y - y_origin, 2))) /
          radius
      );
    };

    const position = (x, y) => {
      y = y - joystick().height;
      if (y > y_origin) {
        y = y_origin;
      }

      const angle = stick().angle(x, y);
      const extention_x = radius * Math.cos(angle) + x_origin;
      const extention_y = radius * Math.sin(angle) + y_origin;

      if (!isInsideCircle(x, y)) {
        x = extention_x;
        y = extention_y;
      }
      return {
        stick_x: x,
        stick_y: y,
        extention_x: extention_x,
        extention_y: extention_y,
      };
    };

    const draw = (x = x_origin, y = y_origin, extention_x, extention_y) => {
      context.clearRect(0, 0, width, height);

      const gradient = context.createRadialGradient(x, y, 0, x, y, radius / 10);
      gradient.addColorStop(0, "rgba(200,200,255,1)");
      gradient.addColorStop(1, "rgba(0,155,255,0.5)");

      context.beginPath();
      context.arc(x, y, radius / 10, 0, Math.PI * 2, true);
      context.fillStyle = gradient;
      context.fill();
      if (onMouseDown) {
        context.lineWidth = 4;
        context.strokeStyle = "rgba(255,161,103,0.5)";
        context.beginPath();
        context.moveTo(x_origin, y_origin);
        context.lineTo(extention_x, extention_y);
        context.stroke();
      }
    };
    return {
      canvas: canvas,
      context: context,
      angle: angle,
      speed: speed,
      position: position,
      draw: draw,
    };
  };

  const background = () => {
    const canvas = document.getElementById("background");
    const context = canvas.getContext("2d");
    const draw = () => {
      const gradient = context.createRadialGradient(
        x_origin,
        y_origin,
        0,
        x_origin,
        y_origin,
        radius
      );
      gradient.addColorStop(0, "rgba(0,222,255,0.5)");
      gradient.addColorStop(1, "rgba(0,222,255,0.05)");

      context.beginPath();
      context.arc(x_origin, y_origin, radius, 0, Math.PI, true);
      context.fillStyle = gradient;
      context.fill();

      context.lineWidth = 4;
      context.strokeStyle = "rgba(0,222,255,0.3)";
      context.stroke();

      context.beginPath();
      context.arc(x_origin, y_origin, radius / 2, 0, Math.PI, true);
      context.stroke();

      context.beginPath();
      context.moveTo(x_origin, y_origin);
      context.lineTo(
        radius * Math.cos(Math.PI / 2) + x_origin,
        -radius * Math.sin(Math.PI / 2) + y_origin
      );
      context.stroke();
    };
    return {
      canvas: canvas,
      context: context,
      draw: draw,
    };
  };

  return {
    height: height,
    x_origin: x_origin,
    y_origin: y_origin,
    radius: radius,
    stick: stick(),
    background: background(),
    resize: () => {
      stick().canvas.width = width;
      stick().canvas.height = height;
      background().canvas.width = width;
      background().canvas.height = height;
    },
  };
};

window.addEventListener("load", () => {
  window.addEventListener("resize", resizeWindow);
  document.addEventListener("mousedown", startTilting);
  document.addEventListener("mousemove", tilt);
  document.addEventListener("mouseup", stopTilting);
  document.addEventListener("touchstart", startTilting);
  document.addEventListener("touchmove", tilt);
  document.addEventListener("touchend", stopTilting);
  document.addEventListener("touchcancel", stopTilting);

  resizeWindow();
});

const getPosition = () => {
  const event = window.event;
  return {
    mouse_x: event.clientX || event.touches[0].clientX,
    mouse_y: event.clientY || event.touches[0].clientY,
  };
};

const resizeWindow = () => {
  joystick().resize();
  joystick().background.draw();
  neutralizeJoystick();
};

const neutralizeJoystick = () => {
  joystick().stick.draw();
  document.getElementById("speed").innerText = 0;
  document.getElementById("speed-meter").style.width = 0 + "%";
  websocket.send("L" + 0);
};

const startTilting = () => {
  onMouseDown = true;
  tilt();
};

const tilt = () => {
  if (onMouseDown) {
    const { mouse_x, mouse_y } = getPosition();
    const { stick_x, stick_y, extention_x, extention_y } =
      joystick().stick.position(mouse_x, mouse_y);
    joystick().stick.draw(stick_x, stick_y, extention_x, extention_y);

    const speed = joystick().stick.speed(stick_x, stick_y);
    document.getElementById("speed").innerText = speed;
    document.getElementById("speed-meter").style.width = speed + "%";
    document.getElementById(
      "speed-meter"
    ).style.background = `linear-gradient(to right, rgba(0, 255, 255, 0.4) 0, rgba(0, 101, 255, 1) ${
      100 * (100 / speed)
    }%)`;
    websocket.send("L" + speed);
  }
};

const stopTilting = () => {
  onMouseDown = false;
  neutralizeJoystick();
};

const isInsideCircle = (x, y) => {
  const currentRadius = Math.sqrt(
    Math.pow(x - joystick().x_origin, 2) + Math.pow(y - joystick().y_origin, 2)
  );
  if (joystick().radius >= currentRadius) {
    return true;
  } else return false;
};
