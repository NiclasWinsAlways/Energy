var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var temperatureData = [];
var temperatureChart;

// Initialize WebSocket and temperature chart when the page loads
window.addEventListener('load', function(event) {
    initWebSocket();
    initTemperatureChart();
});

// Initialize or update the temperature chart
function initTemperatureChart() {
    var ctx = document.getElementById('temperatureGraph').getContext('2d');
    // Destroy the existing chart instance if it exists
    if (temperatureChart) {
        temperatureChart.destroy();
    }
    // Initialize a new chart instance
    temperatureChart = new Chart(ctx, {
        type: 'line',
        data: {
            datasets: [{
                label: 'Temperature',
                data: temperatureData,
                borderColor: 'rgba(75, 192, 192, 1)',
                borderWidth: 2,
                fill: false
            }]
        },
        options: {
            scales: {
                x: {
                    type: 'time',
                    time: {
                        unit: 'second'
                    },
                    ticks: {
                        display: false // Optionally hide x-axis labels
                    }
                },
                y: {
                    beginAtZero: false,
                    ticks: {
                        min: Math.min(...temperatureData.map(data => data.y)),
                        max: Math.max(...temperatureData.map(data => data.y))
                    }
                }
            }
        }
    });
}

// Initialize WebSocket connection
function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = function() {
        console.log('Connection opened');
        getReadings();
    };
    websocket.onclose = function() {
        console.log('Connection closed');
        setTimeout(initWebSocket, 2000);
    };
    websocket.onmessage = function(event) {
        handleWebSocketMessage(event);
    };
}

// Send request to get temperature readings
function getReadings() {
    websocket.send("getReadings");
}

// Handle incoming WebSocket messages
function handleWebSocketMessage(event) {
    var lines = event.data.trim().split('\n');
    lines.forEach(function(line) {
        if (line) {
            var data = JSON.parse(line.trim());
            var temperature = parseFloat(data.temp); // Ensure 'temp' correctly maps to your JSON data key
            var timestamp = new Date(parseInt(data.time)); // Convert 'time' string to integer

            temperatureData.push({ x: timestamp, y: temperature });
            updateTemperatureChart();
            document.getElementById('temperature').textContent = temperature.toFixed(2) + ' °C'; // Ensure only one instance of "°C"
        }
    });
}

// Update the chart with new data
function updateTemperatureChart() {
    if (temperatureData.length > 20) {
        temperatureData.shift(); // Limit the number of data points displayed
    }
    if (temperatureChart) {
        temperatureChart.data.datasets[0].data = temperatureData;
        temperatureChart.options.scales.y.min = Math.min(...temperatureData.map(data => data.y));
        temperatureChart.options.scales.y.max = Math.max(...temperatureData.map(data => data.y));
        temperatureChart.update();
    }
}

// Function to download temperature data as CSV
function downloadCSV() {
    var csv = 'Time,Temperature\n';
    temperatureData.forEach(function(row) {
        csv += `${row.x.toISOString()},${row.y}\n`;
    });
    var csvBlob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
    var csvUrl = URL.createObjectURL(csvBlob);
    var hiddenElement = document.createElement('a');
    hiddenElement.href = csvUrl;
    hiddenElement.target = '_blank';
    hiddenElement.download = 'temperatureData.csv';
    hiddenElement.click();
}

// Clear server-side CSV file
function clearCSV() {
    fetch('/clearcsv')
        .then(response => response.text())
        .then(data => console.log(data));
}

// Clear all data on the graph and reinitialize
function clearGraph() {
    temperatureData = []; // Clear the data array
    updateTemperatureChart(); // Update the chart to reflect the cleared data
}
