document.addEventListener("DOMContentLoaded", function() {
    const form = document.getElementById('wifi-form');
    form.addEventListener('submit', function(event) {
        event.preventDefault();  // Prevent the form from submitting through traditional means.

        const formData = new FormData(form);
        fetch('/setup_wifi', {
            method: 'POST',
            body: formData
        })
        .then(response => response.text())
        .then(data => {
            document.getElementById('response-area').textContent = 'Response: ' + data;
        })
        .catch(error => {
            console.error('Error with WiFi setup:', error);
            document.getElementById('response-area').textContent = 'Error: ' + error;
        });
    });
});
