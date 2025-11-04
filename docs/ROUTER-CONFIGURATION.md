# Router Configuration for Secure External Access

## 🌐 **Overview**

This guide walks through configuring your home router for secure external access to your ESP32 WebRTC device using port forwarding and dynamic DNS, while maintaining strong security posture.

## 🔧 **Router Port Forwarding Setup**

### **Required Port Forwards**

Configure these port forwards in your router's admin panel:

```
External Port → Internal Port → Device IP    → Protocol → Purpose
443          → 443          → 192.168.1.xxx → TCP      → HTTPS Web Interface
5004         → 5004         → 192.168.1.xxx → UDP      → Secure RTP Audio Stream
```

### **Step-by-Step Configuration**

#### **1. Find Your ESP32's IP Address**
```bash
# From ESP32 serial console or web interface
Device IP: 192.168.1.xxx
MAC Address: AA:BB:CC:DD:EE:FF
```

#### **2. Access Router Admin Panel**
```
Common router IPs:
- 192.168.1.1
- 192.168.0.1  
- 10.0.0.1
- 192.168.1.254

Default credentials often:
- admin/admin
- admin/password
- admin/(blank)
```

#### **3. Navigate to Port Forwarding**
Look for sections named:
- Port Forwarding
- Virtual Servers
- NAT/Gaming
- Advanced → Port Forwarding

#### **4. Create HTTPS Rule**
```
Service Name: ESP32-HTTPS
External Port: 443
Internal Port: 443
Protocol: TCP
Internal IP: 192.168.1.xxx (your ESP32)
Status: Enabled
```

#### **5. Create RTP Rule**
```
Service Name: ESP32-RTP
External Port: 5004
Internal Port: 5004
Protocol: UDP
Internal IP: 192.168.1.xxx (your ESP32)
Status: Enabled
```

## 🏠 **Dynamic DNS Configuration**

### **Recommended DDNS Providers**

#### **No-IP (Free Option)**
```
1. Create account at noip.com
2. Create hostname: yourdevice.ddns.net
3. Configure router DDNS:
   - Provider: No-IP
   - Hostname: yourdevice.ddns.net
   - Username: your-noip-username
   - Password: your-noip-password
   - Update Interval: 5 minutes
```

#### **DuckDNS (Free & Simple)**
```
1. Visit duckdns.org
2. Login with social account
3. Create subdomain: yourdevice.duckdns.org
4. Get your token
5. Configure router DDNS:
   - Provider: DuckDNS
   - Domain: yourdevice.duckdns.org
   - Token: your-duck-token
```

#### **Cloudflare (Advanced)**
```
1. Buy domain from registrar
2. Transfer DNS to Cloudflare
3. Create A record: device.yourdomain.com
4. Enable proxy (orange cloud) for DDoS protection
5. Configure router to update Cloudflare via API
```

### **Router DDNS Setup Examples**

#### **ASUS Routers**
```
Administration → DDNS
- Enable DDNS Client: Yes
- Server: No-IP.com
- Host Name: yourdevice.ddns.net
- User Name: your-noip-username
- Password: your-noip-password
- Enable wildcard: No
```

#### **Netgear Routers**
```
Dynamic DNS → No-IP
- Use Dynamic DNS Service: Yes
- Service Provider: No-IP.com
- Host Name: yourdevice.ddns.net
- Username: your-noip-username
- Password: your-noip-password
```

#### **Linksys Routers**
```
Smart Wi-Fi Tools → External Storage
- Dynamic DNS: Enabled
- DDNS Service: No-IP
- Hostname: yourdevice.ddns.net
- Username: your-noip-username  
- Password: your-noip-password
```

## 🛡️ **Router Security Hardening**

### **Firewall Configuration**

#### **Default Deny Policy**
```
1. Set default incoming policy to DENY
2. Only allow specific ports (443, 5004)
3. Enable SPI (Stateful Packet Inspection)
4. Enable DoS protection
```

#### **Advanced Firewall Rules**
```bash
# Allow HTTPS from anywhere
Rule 1: Allow TCP 443 from ANY to 192.168.1.xxx

# Allow RTP only after HTTPS authentication
Rule 2: Allow UDP 5004 from ANY to 192.168.1.xxx
        (Consider restricting to known client IPs)

# Block all other incoming
Rule 3: Deny ALL from ANY to 192.168.1.xxx

# Log blocked attempts
Rule 4: Log denied connections
```

### **Additional Security Features**

#### **Enable Router Security Features**
```
✅ SPI Firewall
✅ DoS Attack Protection  
✅ Port Scan Detection
✅ IP Flood Detection
✅ Access Control (if needed)
✅ VPN Server (recommended alternative)
```

#### **Disable Unnecessary Services**
```
❌ WPS (WiFi Protected Setup)
❌ Remote Management (unless secured)
❌ UPnP (Universal Plug and Play)
❌ Guest Network (if not needed)
❌ SSH/Telnet (unless required)
```

## 🔐 **Enhanced Security Configurations**

### **VPN Alternative (Recommended)**

Instead of direct port forwarding, consider VPN access:

#### **OpenVPN Server Setup**
```bash
# Many routers support built-in OpenVPN server
1. Enable OpenVPN Server in router
2. Generate client certificates
3. Configure port (usually 1194 UDP)
4. Access ESP32 via VPN tunnel: https://192.168.1.xxx
```

#### **WireGuard Setup** (Modern routers)
```bash
# Faster and more secure than OpenVPN
1. Enable WireGuard server
2. Generate client keys
3. Configure allowed IPs
4. Access via secure tunnel
```

### **Network Segmentation**

#### **IoT VLAN Setup**
```
1. Create separate VLAN for IoT devices
2. Assign ESP32 to IoT VLAN (192.168.100.0/24)
3. Configure firewall rules:
   - IoT VLAN → Internet: ALLOW
   - Main LAN → IoT VLAN: ALLOW (management)
   - IoT VLAN → Main LAN: DENY
```

#### **Guest Network Isolation**
```
1. Enable guest network
2. Place ESP32 on guest network
3. Disable guest-to-guest communication
4. Allow internet access only
```

## 📱 **Client Configuration Examples**

### **Web Browser Access**
```
External URL: https://yourdevice.ddns.net
- Certificate warning expected (self-signed)
- Add security exception
- Login with admin credentials
```

### **Mobile App Configuration**
```javascript
// React Native / Flutter app config
const config = {
    deviceURL: 'https://yourdevice.ddns.net',
    rtpPort: 5004,
    validateSSL: false, // For self-signed certs
    timeout: 10000
};
```

### **Python Client Example**
```python
import requests
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

class SecureESP32Client:
    def __init__(self, host):
        self.host = host
        self.token = None
        self.session = requests.Session()
        self.session.verify = False  # For self-signed certs
    
    def login(self, username, password):
        response = self.session.post(
            f'https://{self.host}/api/login',
            data={'username': username, 'password': password}
        )
        if response.status_code == 200:
            self.token = response.json()['token']
            return True
        return False
    
    def start_stream(self):
        headers = {'Authorization': f'Bearer {self.token}'}
        response = self.session.post(
            f'https://{self.host}/api/start-stream',
            headers=headers
        )
        return response.json()

# Usage
client = SecureESP32Client('yourdevice.ddns.net')
if client.login('admin', 'your-password'):
    stream_info = client.start_stream()
    print(f"Stream started on port {stream_info['rtp_port']}")
```

## 🚨 **Security Monitoring**

### **Router Log Monitoring**
```bash
# Monitor for suspicious activity
- Multiple failed authentication attempts
- Port scan attempts
- Unusual traffic patterns
- Unknown IP addresses
```

### **ESP32 Security Logs**
```json
{
  "timestamp": "2025-01-16T18:30:45Z",
  "event": "RATE_LIMIT_EXCEEDED", 
  "client_ip": "203.0.113.42",
  "details": "100 requests in 60 seconds"
}
```

### **Automated Alerts**
```bash
# Set up alerts for:
- Failed login attempts > 5
- Rate limiting triggered
- New IP addresses accessing device
- Unusual data transfer volumes
```

## 🔄 **Maintenance & Updates**

### **Regular Security Tasks**

#### **Weekly**
```
✅ Check router logs for anomalies
✅ Verify DDNS is updating correctly
✅ Test external access functionality
✅ Monitor ESP32 security logs
```

#### **Monthly**
```
✅ Update router firmware
✅ Rotate ESP32 admin password
✅ Review firewall rules
✅ Check for new security features
```

#### **Quarterly**
```
✅ Security audit and penetration test
✅ Review and update DDNS credentials
✅ Evaluate VPN migration
✅ Update emergency recovery procedures
```

### **Backup Configuration**
```bash
# Export router config regularly
1. Access router admin panel
2. System → Backup/Restore
3. Download configuration file
4. Store securely offline

# ESP32 configuration backup
- NVS credentials
- TLS certificates  
- User accounts
```

## 🆘 **Troubleshooting**

### **Common Issues**

#### **External Access Not Working**
```bash
# Check port forwarding
1. Verify router port forward rules
2. Test internal access first: https://192.168.1.xxx
3. Check DDNS resolution: nslookup yourdevice.ddns.net
4. Verify external IP: whatismyip.com
5. Test specific ports: telnet yourdevice.ddns.net 443
```

#### **HTTPS Certificate Warnings**
```bash
# Expected behavior with self-signed certificates
1. Browser will show security warning
2. Click "Advanced" → "Proceed anyway"
3. Or add permanent security exception
4. Consider Let's Encrypt for production
```

#### **RTP Streaming Issues**
```bash
# NAT/Firewall problems
1. Verify UDP 5004 port forward
2. Check client firewall settings
3. Test with different client networks
4. Monitor ESP32 logs for connection attempts
```

This configuration provides secure external access while maintaining strong security posture for your ESP32 WebRTC audio streaming device.