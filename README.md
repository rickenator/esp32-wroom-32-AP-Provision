# ESP32 Secure WebRTC Audio Streaming Device

🎤 **Professional-grade audio streaming system** with ESP32-WROOM-32 and INMP441 digital microphone, featuring **secure external access** via encrypted WebRTC.

## 🌟 **Key Features**

- **🔊 High-Quality Audio**: INMP441 I2S digital microphone with 24-bit capture
- **🌐 WebRTC Streaming**: Real-time audio streaming with G.711 A-law encoding
- **🔐 Enterprise Security**: HTTPS, JWT authentication, SRTP encryption, rate limiting
- **📱 Mobile Access**: Secure access from anywhere via cellphone browser
- **🛡️ Production Ready**: Comprehensive security for internet exposure
- **⚡ Easy Setup**: Complete documentation with step-by-step guides

## 🚀 **Quick Start**

### **📱 For Mobile/External Access (Recommended)**
**👉 Follow [HOWTO.md](HOWTO.md) for complete setup guide!**

```bash
git clone https://github.com/rickenator/esp32-wroom-32-AP-Provision.git
cd esp32-wroom-32-AP-Provision

# Deploy secure firmware
./switch-firmware.sh secure && pio run --target upload

# Then follow HOWTO.md for router setup and external access
```

### **🏠 Local Development**
```bash
# Test hardware first
./switch-firmware.sh inmp441-test && pio run --target upload

# Then basic WebRTC
./switch-firmware.sh webrtc && pio run --target upload
```

## 📚 **Documentation**

- **[HOWTO.md](HOWTO.md)** - ⭐ **START HERE** - Complete setup for mobile access
- **[SECURITY-IMPLEMENTATION.md](SECURITY-IMPLEMENTATION.md)** - Security architecture and threat model
- **[ROUTER-CONFIGURATION.md](ROUTER-CONFIGURATION.md)** - Router setup for external access
- **[THEORY-OF-OPERATIONS.md](THEORY-OF-OPERATIONS.md)** - Technical architecture details
- **[WIRING-INMP441.md](WIRING-INMP441.md)** - Hardware wiring guide

## 🔧 **Hardware Requirements**

- **ESP32-WROOM-32** (DevKitC or compatible)
- **INMP441 Digital Microphone** (I2S interface)
- **WiFi Network** (for local/internet connectivity)
- **Router Admin Access** (for external access setup)

## 🛡️ **Security Features**

- **🔐 HTTPS/TLS Encryption** - Secure web interface with certificates
- **🎫 JWT Authentication** - Token-based access with role-based permissions
- **⚡ Rate Limiting** - DDoS protection (100 requests/minute per IP)
- **🔒 SRTP Encryption** - Encrypted audio streams for privacy
- **📊 Security Logging** - Comprehensive audit trail and monitoring
- **🛑 Brute Force Protection** - Account lockouts after failed attempts

## 📱 **Use Cases**

- **🏠 Home Security**: Remote audio monitoring from anywhere
- **🐕 Pet Monitoring**: Listen to pets while away from home
- **🔧 Equipment Monitoring**: Industrial/workshop audio surveillance
- **👶 Baby Monitor**: Secure audio monitoring with external access
- **🎵 Audio Streaming**: High-quality digital audio distribution

## 🌿 **Branch Structure**

- **`main`** - Production-ready secure WebRTC firmware
- **`feature-webrtc-INMP441`** - Latest development with all features
- **`feature-barebones`** - Simplified WiFi provisioning alternatives

## 🎯 **What Makes This Special**

### **Professional Audio Quality**
- **Digital I2S Interface** eliminates analog noise and interference
- **24-bit Audio Capture** provides studio-quality sound
- **G.711 A-law Encoding** ensures WebRTC compatibility
- **Ring Buffer Processing** maintains real-time performance

### **Enterprise-Grade Security**
- **Multi-layer Protection** suitable for internet exposure
- **Comprehensive Threat Model** with documented security measures
- **Production-Ready** authentication and encryption
- **Audit Logging** for security monitoring and compliance

### **Worldwide Accessibility**
- **Dynamic DNS Support** for easy external access
- **Mobile-Optimized** web interface works on any smartphone
- **Router Integration** with detailed setup guides
- **VPN Alternative** documentation for ultimate security

## 🔄 **Firmware Variants**

| Variant | Purpose | Security Level | Best For |
|---------|---------|----------------|----------|
| `secure` | Production | ⭐⭐⭐⭐⭐ | External access |
| `webrtc` | Development | ⭐⭐⭐ | Local testing |
| `inmp441-test` | Hardware validation | ⭐ | Hardware setup |
| `main/legacy` | Original KY-038 | ⭐⭐ | Legacy support |

## 🚀 **Getting Started**

1. **📖 Read** [HOWTO.md](HOWTO.md) for complete setup instructions
2. **🔌 Wire** INMP441 microphone using [WIRING-INMP441.md](WIRING-INMP441.md)
3. **⚡ Deploy** secure firmware: `./switch-firmware.sh secure`
4. **🌐 Configure** router for external access
5. **📱 Access** from anywhere: `https://yourdevice.ddns.net`

## 🤝 **Contributing**

1. Fork the repository
2. Create feature branch from `feature-webrtc-INMP441`
3. Make changes and test thoroughly
4. Submit pull request with detailed description

## 📄 **License**

Open source project - see repository for license details.

---

**🎉 Transform your ESP32 into a professional, secure, internet-accessible audio streaming device!**