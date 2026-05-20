# ML-Based Smart Farming System

## Overview
This project aims to develop an intelligent farming system using IoT and machine learning. The system integrates sensors, cloud computing, and AI-driven models to optimize irrigation, monitor soil quality, detect pests, and provide predictive yield analysis. The insights are accessible through a mobile/web application.

---

## System Architecture
### Components:
- **Sensors & IoT Devices**: Measure soil moisture, temperature, humidity, and pH.
- **Microcontroller (ESP32)**: Collects sensor data and sends it to the cloud.
- **Cloud Computing**: Stores and processes sensor data.
- **Machine Learning Models**: Predict irrigation needs, detect diseases, and control pests.
- **Web App**: Displays real-time farm conditions and AI-driven insights.

---

## Hardware & Software Requirements
### Hardware:
- Soil moisture sensor (Capacitive/Resistive)
- DHT22 (Temperature & Humidity Sensor)
- pH sensor (Soil acidity measurement)
- Camera module (Pest and disease detection)
- ESP32(Microcontroller)
- Relay module (Controls water pump)

### Software:
- **Backend**: Python (FastAPI), OpenCV & TensorFlow for ML
- **Frontend**: React.js
- **Database**: Firebase/MySQL

---

## Features & Implementation
### 1️⃣ AI-Driven Soil Quality Monitoring
- **ML Model**: Random Forest / Decision Tree
- **Implementation**: Uses `sklearn` for training, deployed on Flask/Django API
- **Live Data**: Fetches real-time sensor data for recommendations

### 2️⃣ Predictive Yield Analysis
- **ML Model**: Linear Regression / LSTM (Time-series prediction)
- **Dataset**: FAO, NASA, local agriculture data
- **Implementation**: Model training in TensorFlow/Keras, deployed via API

### 3️⃣ Smart Pest Control
- **ML Model**: CNN (Convolutional Neural Network)
- **Implementation**: Trained on labeled pest images, real-time detection via mobile app

### 4️⃣ AI-Powered Voice Assistant for Farmers
- **Technology**: SpeechRecognition, gTTS for voice processing
- **NLP Engine**: Rasa/Dialogflow for intent classification
- **Implementation**: Provides farming-related guidance through voice interactions

### 5️⃣ Remote Monitoring via IoT & Mobile App
- **Sensors**: Temperature, Humidity, Soil Moisture
- **Data Processing**: Firebase/MySQL via HTTP
- **Visualization**: React.js web

### 6️⃣ Auto-Weather-Adaptive Fertilization Scheduling
- **Integration**: OpenWeather API for rainfall predictions
- **ML Model**: Regression-based scheduling
- **Implementation**: Notifies farmers about optimal fertilization timing

---

## Deployment Strategy
### Cloud Computing
- **Cloud computing** (GCP) for model deployment and data storage

### Web App Deployment
- Backend hosted on GCP
- Frontend deployed on Netlify + Render
- Push notifications for critical alerts

---

## Future Steps
✅ Train and fine-tune ML models
✅ Optimize IoT power consumption for field deployment
✅ Perform real-world testing in a farm environment
✅ Document findings for final-year project submission

---

## Getting Started
### Installation
```bash
# Clone the repository
git clone https://github.com/Rizwan102003/ml-smart-farming.git
cd ml-smart-farming
```

### Backend Setup (Flask)
```bash
pip install -r requirements.txt
python app.py
```

### Frontend Setup (React)
```bash
cd frontend
npm install
npm start
```

### IoT Device Setup (ESP32)
Upload the firmware using Arduino IDE or PlatformIO.

---

## License
This project is licensed under the MIT License.

---

## Collaborators
- **Sk Md Rizwan** [Group Co-ordinator] - sk.mdrizwan.ece26@heritageit.edu.in [![LinkedIn](https://img.shields.io/badge/LinkedIn-Sk_Md_Rizwan-blue?logo=linkedin)](https://www.linkedin.com/in/skmdrizwan/)
- **Anish Das** - anish.das.ece26@heritageit.edu.in [![LinkedIn](https://img.shields.io/badge/LinkedIn-Anish_Das-blue?logo=linkedin)](https://www.linkedin.com/in/anish-das-73814a27a/)
- **Swaraj Kumar Maity** - swaraj.kumarmaity.ece26@heritageit.edu.in [![LinkedIn](https://img.shields.io/badge/LinkedIn-Swaraj_Kumar_Maity-blue?logo=linkedin)](https://in.linkedin.com/in/swaraj-kumar-maity-164320353)
- **Samudra Mukhar Goswami** - samudra.mukhargoswami.ece26@heritageit.edu.in [![LinkedIn](https://img.shields.io/badge/LinkedIn-Samudra_Mukhar_Goswami-blue?logo=linkedin)](https://www.linkedin.com/in/samudra-goswami)


---
