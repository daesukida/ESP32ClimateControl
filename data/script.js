let desiredTemp = 22; // valor inicial
let ws;

function initWebSocket() {
  ws = new WebSocket(`ws://${window.location.host}/ws`);
  
  ws.onopen = () => {
    console.log('WebSocket connected');
  };

  ws.onmessage = (event) => {
    console.log('WebSocket message received:', event.data);
    try {
      const data = JSON.parse(event.data);
      if ('currentTemp' in data && 'desiredTemp' in data && 'acState' in data) {
        document.getElementById('currentTemp').textContent = data.currentTemp.toFixed(1);
        document.getElementById('desiredTemp').textContent = data.desiredTemp.toFixed(1);
        desiredTemp = data.desiredTemp;
        const acButton = document.getElementById('acButton');
        acButton.textContent = data.acState ? 'Ligado' : 'Desligado';
        acButton.className = 'ac-button' + (data.acState ? ' on' : '');
      } else {
        console.error('Invalid WebSocket data:', data);
      }
    } catch (error) {
      console.error('Error parsing WebSocket message:', error);
    }
  };

  ws.onclose = () => {
    console.log('WebSocket disconnected. Reconnecting...');
    setTimeout(initWebSocket, 5000);
  };

  ws.onerror = (error) => {
    console.error('WebSocket error:', error);
  };
}

function adjustTemperature(change) {
  console.log('Adjusting temperature by:', change, 'Current desiredTemp:', desiredTemp);
  const newTemp = desiredTemp + change;
  if (newTemp >= 17 && newTemp <= 30) {
    desiredTemp = newTemp;
    document.getElementById('desiredTemp').textContent = desiredTemp.toFixed(1);
    console.log('New desiredTemp:', desiredTemp);
    sendDesiredTemp(desiredTemp);
  } else {
    console.log('Temperature out of range (17-30°C):', newTemp);
  }
}

function sendDesiredTemp(temp) {
  const payload = JSON.stringify({ desiredTemp: temp });
  console.log('Sending desired temperature payload:', payload);
  fetch('/desiredTemp', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: payload
  })
  .then(response => {
    if (!response.ok) {
      return response.text().then(text => {
        throw new Error(`HTTP error! Status: ${response.status} ${response.statusText}, Response: ${text}`);
      });
    }
    console.log('Desired temperature sent successfully');
    return response.json();
  })
  .catch(error => console.error('Erro ao enviar temperatura desejada:', error));
}

function toggleAC() {
  console.log('Attempting to toggle AC state');
  fetch('/acToggle', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({})
  })
  .then(response => {
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status} ${response.statusText}`);
    }
    return response.json();
  })
  .then(data => {
    console.log('AC toggle response:', data);
    const acButton = document.getElementById('acButton');
    if ('acState' in data) {
      acButton.textContent = data.acState ? 'Ligado' : 'Desligado';
      acButton.className = 'ac-button' + (data.acState ? ' on' : '');
    } else {
      console.error('Invalid response: acState not found in response');
    }
  })
  .catch(error => {
    console.error('Erro ao alternar ar condicionado:', error.message);
  });
}

window.onload = () => {
  initWebSocket();
};