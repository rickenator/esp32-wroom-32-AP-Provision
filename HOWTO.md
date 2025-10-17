# 📱 HOWTO: Setup ESP32 Secure Audio Streaming for Mobile Access

## 🎯 **Goal**
Set up your ESP32 device for **secure audio streaming** accessible from your cellphone **anywhere in the world** via encrypted WebRTC over the internet.

## 📋 **What You'll Need**
- ESP32-WROOM-32 + INMP441 microphone (wired per [WIRING-INMP441.md](WIRING-INMP441.md))
- Home WiFi router with admin access
- Dynamic DNS account (free options available)
- Cellphone with web browser

## 🚀 **Complete Setup Guide**

### **STEP 1: Deploy Secure Firmware** ⏱️ *5 minutes*

```bash
# 1. Clone and navigate to project
git clone https://github.com/rickenator/esp32-wroom-32-AP-Provision.git
cd esp32-wroom-32-AP-Provision

# 2. Switch to secure firmware and upload
./switch-firmware.sh secure
pio run --target upload

# 3. Open serial monitor to get admin password
pio device monitor
```

**📝 What to look for in serial output:**
```
=== INITIAL ADMIN CREDENTIALS ===
Username: admin
Password: ABCDEFGHIJKLMNOP
=== CHANGE PASSWORD IMMEDIATELY ===
```

**⚠️ IMPORTANT:** Write down this password - you'll need it later!

---

### **STEP 2: Configure WiFi** ⏱️ *2 minutes*

If not already configured:

```bash
# Option A: Use existing WiFi credentials in NVS
# (Device will auto-connect if previously configured)

# Option B: Provision new WiFi via captive portal
# 1. Press and hold button on ESP32 for 2 seconds
# 2. Connect phone to "ESP32-Setup" WiFi network  
# 3. Enter your WiFi credentials in captive portal
# 4. Device will reboot and connect to your network
```

**📝 Note the device IP address** from serial monitor:
```
Connected! IP: 192.168.1.145
```

---

### **STEP 3: Test Local Access** ⏱️ *2 minutes*

Before setting up external access, verify everything works locally:

```bash
# From a device on your home network, visit:
https://192.168.1.145  # (use your device's IP)

# You'll see a security warning (expected with self-signed cert)
# Click "Advanced" → "Proceed to 192.168.1.145 (unsafe)"
```

**🔐 Login with:**
- Username: `admin`
- Password: *(the random password from Step 1)*

**✅ You should see:** Secure audio monitor dashboard with green security badges

---

### **STEP 4: Set Up Dynamic DNS** ⏱️ *10 minutes*

#### **Option A: No-IP (Free, Easy)**

1. **Create Account**: Go to [noip.com](https://www.noip.com) → Sign up (free)

2. **Create Hostname**: 
   - Click "Create Hostname"
   - Choose: `yourdevice.ddns.net` (or available name)
   - Leave IP as auto-detected
   - Click "Create Hostname"

3. **Configure Router DDNS**:
   - Access router admin (usually `192.168.1.1`)
   - Navigate to: **Administration** → **DDNS** (or similar)
   - Settings:
     ```
     Enable DDNS: Yes
     Service Provider: No-IP.com
     Hostname: yourdevice.ddns.net
     Username: your-noip-username
     Password: your-noip-password
     ```
   - Save settings

#### **Option B: DuckDNS (Free, Simple)**

1. **Get Token**: Visit [duckdns.org](https://www.duckdns.org) → Login with Google/GitHub
2. **Create Domain**: Type desired name → Click "add domain" → Get your token
3. **Configure Router**: Use DuckDNS settings with your domain and token

---

### **STEP 5: Configure Router Port Forwarding** ⏱️ *5 minutes*

**⚠️ CRITICAL:** These exact ports must be forwarded for secure access:

#### **Access Router Settings**
```
# Common router IPs:
192.168.1.1  (most common)
192.168.0.1  
10.0.0.1
192.168.1.254

# Default login often: admin/admin, admin/password, or admin/(blank)
```

#### **Create Port Forward Rules**

Navigate to: **Advanced** → **Port Forwarding** (or **Virtual Servers**, **NAT/Gaming**)

**Rule 1: HTTPS Web Interface**
```
Service Name: ESP32-HTTPS
External Port: 443
Internal Port: 443
Protocol: TCP
Internal IP: 192.168.1.145  # Your ESP32's IP
Status: Enabled
```

**Rule 2: Secure RTP Audio**
```
Service Name: ESP32-RTP
External Port: 5004
Internal Port: 5004
Protocol: UDP
Internal IP: 192.168.1.145  # Your ESP32's IP
Status: Enabled
```

**🔒 Security Settings** (if available):
- Enable SPI Firewall: ✅ Yes
- Enable DoS Protection: ✅ Yes  
- Block Port Scans: ✅ Yes

---

### **STEP 6: Test External Access** ⏱️ *5 minutes*

#### **From Your Home Network**
```bash
# Test DDNS resolution
nslookup yourdevice.ddns.net
# Should return your public IP address

# Test HTTPS access
https://yourdevice.ddns.net
# Should show your secure ESP32 interface
```

#### **From Cellular/External Network**
1. **Turn off WiFi** on your phone (use cellular data)
2. **Open browser** → Go to: `https://yourdevice.ddns.net`
3. **Accept security warning** (self-signed certificate)
4. **Login** with admin credentials
5. **Test audio streaming** → Click "Start Secure Stream"

**✅ Success indicators:**
- Green "Connected" status with live indicator
- Security badges showing "SECURED" 
- Stream starts with Session ID and encryption notice

---

### **STEP 7: Mobile Client Usage** ⏱️ *Ongoing*

#### **Daily Usage Flow**
1. **Open browser** on phone (Chrome, Safari, Firefox)
2. **Navigate to** `https://yourdevice.ddns.net`
3. **Accept certificate** (one-time per browser)
4. **Login** with credentials
5. **Start stream** → Audio streaming begins with SRTP encryption
6. **Monitor** device status and security logs

#### **What You'll See**
```
🎤 ESP32 Secure Audio Monitor [SECURED]

🛡️ Security Features Active
✅ HTTPS/TLS    ✅ JWT Authentication    ✅ Rate Limiting
✅ SRTP Encryption    ✅ Audit Logging    ✅ Brute Force Protection

Device Status:
🟢 Connected - 0 Active Streams - 45m Uptime - 234KB Free Memory

🎵 Audio Stream Controls
▶️ Start Secure Stream

📡 Stream Configuration (when active):
Session ID: 192.168.1.145_1729123456
RTP Port: 5004
Sample Rate: 8000 Hz  
Codec: PCMA
Encryption: 🔐 SRTP Enabled
```

---

## 🔧 **Advanced Configuration**

### **Change Admin Password**
Currently requires re-flashing firmware. **Planned enhancement:** Web-based password change.

### **Add Multiple Users** 
Edit `secure-webrtc-firmware.cpp` and add to `initializeDefaultUsers()` function:
```cpp
UserCredentials user;
user.username = "listener";
user.passwordHash = hashPassword("your-password", "listener");
user.level = USER_ACCESS;  // Can stream but not configure
users["listener"] = user;
```

### **Custom Certificates**
Replace self-signed certificates in firmware with your own:
1. Generate certificates with Let's Encrypt or commercial CA
2. Update `server_cert` and `server_key` in firmware
3. Recompile and upload

### **VPN Alternative** (More Secure)
Instead of port forwarding, set up VPN server on router:
1. Enable OpenVPN or WireGuard server
2. Generate client certificates
3. Connect via VPN, access device at local IP: `https://192.168.1.145`

---

## 🚨 **Security Best Practices**

### **Strong Password Policy**
- **Minimum 12 characters**
- **Mix of**: uppercase, lowercase, numbers, symbols
- **Example good password**: `MyDog!Barks@3AM#2025`

### **Network Security**
- **Change default router admin password**
- **Enable router firewall and intrusion detection**
- **Regularly check router logs for suspicious activity**
- **Keep router firmware updated**

### **Device Security**
- **Monitor security logs** via web interface
- **Check for unusual access patterns**
- **Restart device monthly** to clear any potential issues
- **Keep firmware updated** when new versions available

### **Access Control**
- **Don't share admin credentials**
- **Use guest network** for IoT devices if possible
- **Consider VPN access** for ultimate security

---

## 🔍 **Troubleshooting**

### **"Can't Connect" Issues**

#### **From Home Network:**
```bash
# Check device is online
ping yourdevice.ddns.net

# Check HTTPS port
telnet yourdevice.ddns.net 443

# Check local access first
https://192.168.1.145
```

#### **From External Network:**
```bash
# Verify DDNS resolution
nslookup yourdevice.ddns.net
# Should return your home's public IP

# Check router port forwards are active
# Use online port checker tools
```

### **"Security Warning" in Browser**
**✅ NORMAL** - Self-signed certificates always show warnings
- **Chrome**: Click "Advanced" → "Proceed to yourdevice.ddns.net (unsafe)"  
- **Safari**: Click "Show Details" → "visit this website"
- **Firefox**: Click "Advanced" → "Accept the risk and continue"

### **"Authentication Failed"**
- **Check password carefully** (case-sensitive)
- **Try refreshing page** (clear any cached attempts)
- **Wait 5 minutes** if locked out (brute force protection)
- **Check ESP32 serial console** for security logs

### **"Stream Won't Start"**
- **Check UDP port 5004** is forwarded correctly
- **Try different cellular network** (some carriers block ports)
- **Check device logs** in web interface
- **Verify authentication token** is valid (login again)

### **"Slow/Choppy Audio"**
- **Check cellular signal strength** (need good connection)
- **Try different audio codec** (future firmware enhancement)
- **Monitor device memory** usage in status panel
- **Check for network congestion**

---

## 📞 **Real-World Usage Scenarios**

### **Home Security Monitoring**
- **Place ESP32** in strategic location (front door, baby room, etc.)
- **Access remotely** to monitor audio in real-time
- **Set up alerts** via webhook integration (future enhancement)

### **Pet Monitoring**
- **Monitor pet sounds** when away from home
- **Check for barking, distress** via real-time audio
- **Integration possibilities** with smart home systems

### **Equipment Monitoring** 
- **Industrial/workshop** audio monitoring
- **Detect unusual machine sounds** remotely
- **Maintenance scheduling** based on audio patterns

---

## 🎯 **Success Criteria**

**✅ You've succeeded when you can:**
1. **Access device securely** via `https://yourdevice.ddns.net` from cellular
2. **Login with admin credentials** and see security dashboard
3. **Start encrypted audio stream** and receive Session ID
4. **Monitor device status** including uptime and memory usage
5. **View security features** showing all protections active

**🚀 Your ESP32 is now a professional-grade, internet-accessible, secure audio streaming device!**

---

## 📚 **Next Steps & Enhancements**

### **Planned Features**
- **Web-based password management**
- **Multiple user account creation via UI**
- **Audio recording and playback**
- **Integration with home automation systems**
- **Mobile app development**

### **Advanced Integration**
- **MQTT integration** for IoT ecosystems
- **Webhook notifications** for audio events  
- **Cloud storage** for audio recordings
- **AI/ML audio analysis** for smart alerts

### **Related Documentation**
- **[SECURITY-IMPLEMENTATION.md](SECURITY-IMPLEMENTATION.md)** - Deep dive into security architecture
- **[ROUTER-CONFIGURATION.md](ROUTER-CONFIGURATION.md)** - Advanced router setup options
- **[THEORY-OF-OPERATIONS.md](THEORY-OF-OPERATIONS.md)** - Complete technical documentation

**🎉 Congratulations! You now have a secure, professional audio streaming device accessible from anywhere in the world!**