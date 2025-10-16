# ESP32 WebRTC Security Implementation Guide

## 🔒 **Security Overview**

When exposing an ESP32 WebRTC audio streaming device to external networks via port forwarding and dynamic DNS, implementing robust security is critical. This document outlines comprehensive security measures to protect against unauthorized access, eavesdropping, and device compromise.

## 🚨 **Threat Model**

### **Attack Vectors:**
1. **Unauthorized Audio Access** - Eavesdropping on audio streams
2. **Device Configuration Tampering** - Changing WiFi settings or audio parameters
3. **Credential Harvesting** - Extracting stored WiFi passwords
4. **Denial of Service** - Overwhelming device with requests
5. **Man-in-the-Middle** - Intercepting unencrypted communications
6. **Brute Force Attacks** - Attempting to guess passwords
7. **Network Scanning** - Discovering and profiling the device

### **Assets to Protect:**
- Audio stream data (privacy-sensitive)
- WiFi credentials stored in NVS
- Device configuration and settings
- Network topology information
- Device availability and performance

## 🛡️ **Security Implementation Layers**

### **Layer 1: Network Security**

#### **Router Configuration:**
```
Recommended Port Forwarding Setup:
External Port → Internal Port → Protocol → Notes
8443         → 443           → TCP     → HTTPS Web Interface
5004         → 5004          → UDP     → RTP Audio (with auth)
```

#### **Dynamic DNS Security:**
```
ddns.example.com → Your Dynamic IP
├─ Enable DDNS authentication
├─ Use strong DDNS account passwords  
├─ Enable DDNS update logging
└─ Consider DDNS service with DDoS protection
```

### **Layer 2: Authentication & Authorization**

#### **Multi-Level Authentication:**
```cpp
// Security levels for different operations
enum SecurityLevel {
    PUBLIC_READ,      // Audio status, basic info
    USER_ACCESS,      // Audio streaming, monitoring
    ADMIN_ACCESS,     // Configuration changes
    SUPER_ADMIN       // NVS flush, factory reset
};

struct UserCredentials {
    String username;
    String passwordHash;  // SHA-256 hashed
    SecurityLevel level;
    uint32_t lastLogin;
    uint16_t failedAttempts;
    bool isLocked;
};
```

#### **Password Policy Implementation:**
```cpp
bool validatePassword(const String& password) {
    // Minimum 12 characters
    if (password.length() < 12) return false;
    
    bool hasUpper = false, hasLower = false, hasDigit = false, hasSpecial = false;
    
    for (char c : password) {
        if (c >= 'A' && c <= 'Z') hasUpper = true;
        else if (c >= 'a' && c <= 'z') hasLower = true;
        else if (c >= '0' && c <= '9') hasDigit = true;
        else if (strchr("!@#$%^&*()_+-=[]{}|;:,.<>?", c)) hasSpecial = true;
    }
    
    return hasUpper && hasLower && hasDigit && hasSpecial;
}
```

### **Layer 3: Encryption & Secure Communications**

#### **HTTPS Implementation:**
```cpp
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include "cert.h"  // Self-signed certificate

WebServer httpsServer(443);
WiFiClientSecure secureClient;

void setupHTTPS() {
    // Load TLS certificate and private key
    secureClient.setCACert(ca_cert);
    secureClient.setCertificate(client_cert);
    secureClient.setPrivateKey(client_key);
    
    // Configure TLS settings
    secureClient.setInsecure(false);  // Require certificate validation
    
    httpsServer.begin(443);
    LOGI("HTTPS server started on port 443");
}
```

#### **TLS Certificate Management:**
```cpp
// Generate self-signed certificate for development
// For production, use Let's Encrypt or commercial CA

const char* ca_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDXTCCAkWgAwIBAgIJAL... (Your Certificate)
-----END CERTIFICATE-----
)EOF";

const char* client_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIC5jCCAc6gAwIBAgIBATANBgkqhkiG9w0BAQsFADA...
-----END CERTIFICATE-----
)EOF";

const char* client_key = R"EOF(
-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQC...
-----END PRIVATE KEY-----
)EOF";
```

#### **Secure RTP (SRTP) Implementation:**
```cpp
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"

class SecureRTP {
private:
    mbedtls_aes_context aes_ctx;
    uint8_t encryption_key[32];  // 256-bit key
    uint8_t authentication_key[20];  // HMAC-SHA1 key
    
public:
    bool initialize(const uint8_t* master_key, const uint8_t* master_salt) {
        // Derive encryption and authentication keys
        deriveKeys(master_key, master_salt);
        
        // Initialize AES context
        mbedtls_aes_init(&aes_ctx);
        mbedtls_aes_setkey_enc(&aes_ctx, encryption_key, 256);
        
        return true;
    }
    
    size_t encryptRTPPacket(uint8_t* packet, size_t length) {
        // AES-CTR encryption for payload
        uint8_t iv[16];
        generateIV(packet, iv);  // Derive IV from RTP header
        
        // Encrypt payload (skip RTP header)
        mbedtls_aes_crypt_ctr(&aes_ctx, length - RTP_HEADER_SIZE,
                              &offset, iv, stream_block,
                              packet + RTP_HEADER_SIZE,
                              packet + RTP_HEADER_SIZE);
        
        // Add HMAC-SHA1 authentication tag
        uint8_t auth_tag[10];
        computeHMAC(packet, length, auth_tag);
        memcpy(packet + length, auth_tag, 10);
        
        return length + 10;  // Original + auth tag
    }
};
```

### **Layer 4: Session Management**

#### **JWT Token Implementation:**
```cpp
#include "mbedtls/md.h"
#include "ArduinoJson.h"

class JWTManager {
private:
    String secret_key;
    uint32_t token_lifetime;  // seconds
    
public:
    String generateToken(const String& username, SecurityLevel level) {
        // Create JWT header
        DynamicJsonDocument header(256);
        header["alg"] = "HS256";
        header["typ"] = "JWT";
        
        // Create JWT payload
        DynamicJsonDocument payload(512);
        payload["sub"] = username;
        payload["lvl"] = (int)level;
        payload["iat"] = getCurrentTimestamp();
        payload["exp"] = getCurrentTimestamp() + token_lifetime;
        payload["iss"] = getDeviceID();
        
        // Encode and sign
        String headerB64 = base64Encode(header);
        String payloadB64 = base64Encode(payload);
        String signature = signHMAC(headerB64 + "." + payloadB64);
        
        return headerB64 + "." + payloadB64 + "." + signature;
    }
    
    bool validateToken(const String& token, SecurityLevel required_level) {
        // Parse token parts
        int firstDot = token.indexOf('.');
        int secondDot = token.indexOf('.', firstDot + 1);
        
        if (firstDot == -1 || secondDot == -1) return false;
        
        String headerB64 = token.substring(0, firstDot);
        String payloadB64 = token.substring(firstDot + 1, secondDot);
        String signature = token.substring(secondDot + 1);
        
        // Verify signature
        String expected = signHMAC(headerB64 + "." + payloadB64);
        if (signature != expected) return false;
        
        // Parse and validate payload
        DynamicJsonDocument payload(512);
        deserializeJson(payload, base64Decode(payloadB64));
        
        // Check expiration
        if (payload["exp"] < getCurrentTimestamp()) return false;
        
        // Check authorization level
        if (payload["lvl"] < (int)required_level) return false;
        
        return true;
    }
};
```

### **Layer 5: Rate Limiting & DDoS Protection**

#### **Request Rate Limiting:**
```cpp
class RateLimiter {
private:
    struct ClientInfo {
        IPAddress ip;
        uint32_t requests;
        uint32_t window_start;
        bool is_blocked;
    };
    
    std::map<String, ClientInfo> clients;
    uint32_t max_requests_per_window = 100;  // per minute
    uint32_t window_duration = 60000;  // 1 minute in ms
    uint32_t block_duration = 300000;  // 5 minutes
    
public:
    bool isAllowed(IPAddress client_ip) {
        String ip_str = client_ip.toString();
        uint32_t now = millis();
        
        auto& client = clients[ip_str];
        
        // Check if client is blocked
        if (client.is_blocked && 
            (now - client.window_start) < block_duration) {
            return false;
        }
        
        // Reset window if expired
        if ((now - client.window_start) > window_duration) {
            client.requests = 0;
            client.window_start = now;
            client.is_blocked = false;
        }
        
        client.requests++;
        
        // Block if over limit
        if (client.requests > max_requests_per_window) {
            client.is_blocked = true;
            LOGW("Rate limit exceeded for %s", ip_str.c_str());
            return false;
        }
        
        return true;
    }
};
```

### **Layer 6: Audit Logging & Monitoring**

#### **Security Event Logging:**
```cpp
enum SecurityEvent {
    LOGIN_SUCCESS,
    LOGIN_FAILURE,
    INVALID_TOKEN,
    RATE_LIMIT_EXCEEDED,
    CONFIG_CHANGE,
    SUSPICIOUS_REQUEST,
    BRUTE_FORCE_DETECTED
};

class SecurityLogger {
private:
    QueueHandle_t logQueue;
    
public:
    void logEvent(SecurityEvent event, IPAddress client_ip, 
                  const String& details = "") {
        DynamicJsonDocument logEntry(512);
        logEntry["timestamp"] = getCurrentTimestamp();
        logEntry["event"] = getEventName(event);
        logEntry["client_ip"] = client_ip.toString();
        logEntry["details"] = details;
        logEntry["device_id"] = getDeviceID();
        
        // Queue for background processing
        String logStr;
        serializeJson(logEntry, logStr);
        
        if (xQueueSend(logQueue, &logStr, pdMS_TO_TICKS(100)) != pdTRUE) {
            LOGW("Security log queue full");
        }
    }
    
    void processLogs() {
        String logEntry;
        while (xQueueReceive(logQueue, &logEntry, 0) == pdTRUE) {
            // Send to SIEM/log server if configured
            sendToLogServer(logEntry);
            
            // Store locally (circular buffer)
            storeLocalLog(logEntry);
            
            // Check for attack patterns
            analyzeForThreats(logEntry);
        }
    }
};
```

## 🔧 **Implementation in WebRTC Firmware**

### **Enhanced Web Server with Security:**
```cpp
void setupSecureWebServer() {
    // Initialize security components
    rateLimiter = new RateLimiter();
    jwtManager = new JWTManager("your-secret-key", 3600);  // 1 hour tokens
    securityLogger = new SecurityLogger();
    
    // Secure authentication endpoint
    httpsServer.on("/api/login", HTTP_POST, [](AsyncWebServerRequest *request) {
        // Rate limiting check
        if (!rateLimiter->isAllowed(request->client()->remoteIP())) {
            securityLogger->logEvent(RATE_LIMIT_EXCEEDED, request->client()->remoteIP());
            request->send(429, "text/plain", "Too Many Requests");
            return;
        }
        
        // Parse credentials
        String username = request->arg("username");
        String password = request->arg("password");
        
        // Validate credentials
        if (authenticateUser(username, password)) {
            String token = jwtManager->generateToken(username, getUserLevel(username));
            
            DynamicJsonDocument response(256);
            response["token"] = token;
            response["expires_in"] = 3600;
            
            String responseStr;
            serializeJson(response, responseStr);
            
            securityLogger->logEvent(LOGIN_SUCCESS, request->client()->remoteIP(), username);
            request->send(200, "application/json", responseStr);
        } else {
            securityLogger->logEvent(LOGIN_FAILURE, request->client()->remoteIP(), username);
            incrementFailedAttempts(username);
            request->send(401, "text/plain", "Invalid credentials");
        }
    });
    
    // Protected audio stream endpoint
    httpsServer.on("/api/audio-stream", HTTP_GET, [](AsyncWebServerRequest *request) {
        String token = request->getHeader("Authorization");
        if (token.startsWith("Bearer ")) {
            token = token.substring(7);
        }
        
        if (!jwtManager->validateToken(token, USER_ACCESS)) {
            securityLogger->logEvent(INVALID_TOKEN, request->client()->remoteIP());
            request->send(401, "text/plain", "Invalid or expired token");
            return;
        }
        
        // Provide WebRTC connection info
        DynamicJsonDocument response(512);
        response["rtp_port"] = RTP_PORT;
        response["ice_servers"] = getICEServers();
        response["device_id"] = getDeviceID();
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
    });
}
```

### **Secure RTP Configuration:**
```cpp
void startSecureAudioStream(const String& client_id, const uint8_t* master_key) {
    // Initialize SRTP
    SecureRTP srtp;
    if (!srtp.initialize(master_key, master_salt)) {
        LOGE("Failed to initialize SRTP");
        return;
    }
    
    // Create secure UDP connection
    WiFiUDP secureUDP;
    secureUDP.begin(RTP_PORT);
    
    // Audio streaming loop with encryption
    while (streamingActive) {
        if (audioDataAvailable()) {
            uint8_t rtpPacket[MAX_RTP_PACKET_SIZE];
            size_t packetSize = generateRTPPacket(rtpPacket);
            
            // Encrypt packet
            size_t encryptedSize = srtp.encryptRTPPacket(rtpPacket, packetSize);
            
            // Send encrypted packet
            secureUDP.beginPacket(client_ip, client_port);
            secureUDP.write(rtpPacket, encryptedSize);
            secureUDP.endPacket();
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));  // 20ms frame timing
    }
}
```

## 📱 **Client-Side Security Implementation**

### **Secure Web Client:**
```javascript
class SecureESP32AudioClient {
    constructor(deviceURL, credentials) {
        this.deviceURL = deviceURL;
        this.credentials = credentials;
        this.token = null;
        this.tokenExpiry = null;
    }
    
    async authenticate() {
        try {
            const response = await fetch(`${this.deviceURL}/api/login`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(this.credentials)
            });
            
            if (response.ok) {
                const data = await response.json();
                this.token = data.token;
                this.tokenExpiry = Date.now() + (data.expires_in * 1000);
                return true;
            } else {
                throw new Error('Authentication failed');
            }
        } catch (error) {
            console.error('Authentication error:', error);
            return false;
        }
    }
    
    async connectToAudioStream() {
        // Ensure we have a valid token
        if (!this.token || Date.now() > this.tokenExpiry) {
            if (!await this.authenticate()) {
                throw new Error('Authentication required');
            }
        }
        
        // Get stream configuration
        const response = await fetch(`${this.deviceURL}/api/audio-stream`, {
            headers: {
                'Authorization': `Bearer ${this.token}`
            }
        });
        
        if (!response.ok) {
            throw new Error('Failed to get stream configuration');
        }
        
        const config = await response.json();
        
        // Set up secure WebRTC connection
        this.pc = new RTCPeerConnection({
            iceServers: config.ice_servers
        });
        
        // Enable DTLS-SRTP for encrypted media
        this.pc.setConfiguration({
            ...this.pc.getConfiguration(),
            bundlePolicy: 'max-bundle',
            rtcpMuxPolicy: 'require'
        });
        
        return this.establishConnection(config);
    }
}
```

## 🔐 **Security Configuration Guide**

### **Initial Setup Checklist:**

1. **Change Default Passwords:**
```cpp
// In NVS storage during first boot
prefs.begin("security", false);
if (!prefs.isKey("admin_hash")) {
    String defaultPassword = generateSecurePassword();
    Serial.printf("INITIAL ADMIN PASSWORD: %s\n", defaultPassword.c_str());
    prefs.putString("admin_hash", hashPassword(defaultPassword));
}
prefs.end();
```

2. **Generate TLS Certificates:**
```bash
# Self-signed certificate for development
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes

# For production, use Let's Encrypt
certbot certonly --manual --preferred-challenges dns -d your-device.ddns.net
```

3. **Configure Firewall Rules:**
```
Router Firewall Configuration:
├─ Block all incoming by default
├─ Allow HTTPS (443) from anywhere
├─ Allow RTP (5004 UDP) only after authentication
├─ Enable DDoS protection
└─ Log all connection attempts
```

### **Operational Security:**

1. **Regular Security Updates:**
   - Monitor ESP32 security advisories
   - Update firmware regularly
   - Rotate authentication keys quarterly

2. **Monitoring & Alerting:**
   - Set up log aggregation
   - Monitor failed authentication attempts
   - Alert on suspicious network activity

3. **Backup & Recovery:**
   - Secure configuration backups
   - Factory reset procedures
   - Key recovery mechanisms

## 🚨 **Security Best Practices**

### **Network Level:**
- Use WPA3 for WiFi when possible
- Implement network segmentation (IoT VLAN)
- Enable router security features (DDoS protection, intrusion detection)
- Use VPN for remote access when possible

### **Application Level:**
- Implement proper session timeout
- Use secure random number generation
- Validate all input parameters
- Implement proper error handling (don't leak information)

### **Operational Level:**
- Regular security audits
- Penetration testing
- Security incident response plan
- User security training

This comprehensive security implementation provides multiple layers of protection suitable for exposing your ESP32 WebRTC device to external networks while maintaining strong security posture.