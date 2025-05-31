#pragma once
#include <System/System.h>
#include <Utilities/Crypto/HMAC/HMAC.h>
#include <Utilities/Crypto/SHA/SHA256.h>
#include <Utilities/Encoding/Base/Base64.h>
#include <Utilities/Serialization/JSON/JSON.h>
#include <array>
#include <span>
#include <string_view>
#include <algorithm>

/*
JWT (JSON Web Token) implementation for embedded systems
Creates and verifies signed tokens for authentication and data exchange

Basic JWT creation:
    JWT::Header header;
    JWT::Claims claims;
    claims.sub = "user123";
    claims.iat = getCurrentTimestamp();
    
    std::array<char, 512> tokenBuffer{};
    size_t tokenLength;
    bool success = JWT::CreateToken(std::span(tokenBuffer), tokenLength, 
                                   header, claims, "my-secret");

Device authentication:
    JWT::Claims deviceClaims;
    deviceClaims.iss = "my-device";
    deviceClaims.sub = "device-001";
    deviceClaims.aud = "api-server";
    deviceClaims.AddCustomClaim("device_type", "sensor");
    deviceClaims.AddCustomClaim("firmware", "v1.2.3");
    
    std::array<char, 1024> token{};
    size_t len;
    JWT::CreateToken(std::span(token), len, header, deviceClaims, secret);

Simple verification:
    const char* receivedToken = "eyJhbGc...";
    const char* secret = "my-secret";
    bool isValid = JWT::VerifyToken(receivedToken, secret);

Full verification with parsing:
    JWT::Header verifiedHeader;
    JWT::Claims verifiedClaims;
    std::array<char, 256> headerBuffer{};
    std::array<char, 512> claimsBuffer{};
    std::array<char, 64> buffer1{}, buffer2{}, buffer3{}, buffer4{};
    
    bool isValid = JWT::VerifyToken(token, secret, verifiedHeader, verifiedClaims,
                                   std::span(headerBuffer), std::span(claimsBuffer),
                                   std::span(buffer1), std::span(buffer2),
                                   std::span(buffer3), std::span(buffer4));

API token for requests:
    JWT::Claims apiClaims;
    apiClaims.iss = "embedded-device";
    apiClaims.aud = "api.example.com";
    apiClaims.exp = currentTime + 3600;  // 1 hour
    apiClaims.AddCustomClaim("scope", "read_sensors");
    
Session management:
    JWT::Claims sessionClaims;
    sessionClaims.sub = userId;
    sessionClaims.iat = sessionStart;
    sessionClaims.exp = sessionStart + sessionDuration;
    sessionClaims.AddCustomClaim("role", "admin");
    sessionClaims.AddCustomClaim("permissions", "read,write");

Token validation:
    if (JWT::IsTokenExpired(claims, currentTime)) {
        // Handle expired token
    }
    if (JWT::IsTokenValid(claims, currentTime)) {
        // Token is valid and not expired
    }

Custom claims access:
    auto deviceType = claims.GetCustomClaim("device_type");
    auto firmware = claims.GetCustomClaim("firmware");
*/

class JWT {
public:
    enum class Algorithm {
        HS256
    };

    struct Header {
        Algorithm alg = Algorithm::HS256;
        std::string_view typ = "JWT";
    };

    struct Claims {
        std::string_view iss;  // issuer
        std::string_view sub;  // subject
        std::string_view aud;  // audience
        uint64 exp = 0;  // expiration time
        uint64 nbf = 0;  // not before
        uint64 iat = 0;  // issued at
        std::string_view jti;  // JWT ID

        // Custom claims storage
        std::array<std::pair<std::string_view, std::string_view>, 10> customClaims{};
        size_t customClaimsCount = 0;

        void AddCustomClaim(std::string_view key, std::string_view value) {
            if (customClaimsCount < customClaims.size()) {
                customClaims[customClaimsCount] = {key, value};
                customClaimsCount++;
            }
        }

        std::string_view GetCustomClaim(std::string_view key) const {
            for (size_t i = 0; i < customClaimsCount; ++i) {
                if (customClaims[i].first == key) {
                    return customClaims[i].second;
                }
            }
            return std::string_view{};
        }
    };

private:
    static constexpr std::string_view AlgorithmToString(Algorithm alg) {
        switch (alg) {
            case Algorithm::HS256: return "HS256";
            default: return "HS256";
        }
    }

    static Algorithm StringToAlgorithm(std::string_view str) {
        if (str == "HS256") return Algorithm::HS256;
        return Algorithm::HS256; // Default
    }

    static bool CreateHeaderJson(std::span<char> buffer, size_t& length, const Header& header) {
        JSON::Writer writer(buffer);

        writer.StartObject()
              .KeyValue("alg", AlgorithmToString(header.alg))
              .Comma()
              .KeyValue("typ", header.typ)
              .EndObject();

        if (writer.HasError()) {
            length = 0;
            return false;
        }

        length = writer.GetLength();
        return true;
    }

    static bool CreateClaimsJson(std::span<char> buffer, size_t& length, const Claims& claims) {
        JSON::Writer writer(buffer);

        writer.StartObject();

        bool first = true;
        auto addField = [&](std::string_view key, std::string_view value) {
            if (!value.empty()) {
                if (!first) writer.Comma();
                writer.KeyValue(key, value);
                first = false;
            }
        };

        auto addNumField = [&](std::string_view key, uint64 value) {
            if (value != 0) {
                if (!first) writer.Comma();
                writer.KeyValue(key, value);
                first = false;
            }
        };

        addField("iss", claims.iss);
        addField("sub", claims.sub);
        addField("aud", claims.aud);
        addNumField("exp", claims.exp);
        addNumField("nbf", claims.nbf);
        addNumField("iat", claims.iat);
        addField("jti", claims.jti);

        // Add custom claims
        for (size_t i = 0; i < claims.customClaimsCount; ++i) {
            addField(claims.customClaims[i].first, claims.customClaims[i].second);
        }

        writer.EndObject();

        if (writer.HasError()) {
            length = 0;
            return false;
        }

        length = writer.GetLength();
        return true;
    }

    static size_t Base64UrlEncode(std::span<char> output, std::span<const uint8> data) {
        size_t base64Length = Base64::Encode(output, data);

        size_t pos = 0;
        for (size_t i = 0; i < base64Length; ++i) {
            char c = output[i];
            if (c == '+') c = '-';
            else if (c == '/') c = '_';
            else if (c == '=') break;

            output[pos++] = c;
        }

        return pos;
    }

    static size_t Base64UrlEncode(std::span<char> output, std::string_view str) {
        return Base64UrlEncode(output, std::span(reinterpret_cast<const uint8*>(str.data()), str.size()));
    }

    static bool Base64UrlDecode(std::span<char> output, std::string_view encoded, size_t& outputSize) {
        // Calculate max size needed for padded buffer
        size_t maxPaddedSize = encoded.size() + 4; // worst case padding
        
        // Use dynamic allocation for large payloads, or stack for small ones
        if (maxPaddedSize <= 512) {
            std::array<char, 512> paddedBuffer{};
            size_t paddedPos = 0;

            // Copy and convert base64url to base64
            for (char c : encoded) {
                if (paddedPos >= paddedBuffer.size() - 4) return false;

                if (c == '-') paddedBuffer[paddedPos++] = '+';
                else if (c == '_') paddedBuffer[paddedPos++] = '/';
                else paddedBuffer[paddedPos++] = c;
            }

            // Add padding
            while (paddedPos % 4 != 0 && paddedPos < paddedBuffer.size()) {
                paddedBuffer[paddedPos++] = '=';
            }

            std::string_view paddedView(paddedBuffer.data(), paddedPos);
            return Base64::DecodeToBuffer(paddedView, output, outputSize);
        } else {
            // For larger payloads, decode directly without intermediate buffer
            // This is less efficient but handles any size
            outputSize = 0;
            size_t pos = 0;
            
            // Process each group of 4 characters
            std::array<char, 4> group;
            size_t groupPos = 0;
            
            for (size_t i = 0; i < encoded.size(); ++i) {
                char c = encoded[i];
                
                // Convert base64url to base64
                if (c == '-') c = '+';
                else if (c == '_') c = '/';
                
                group[groupPos++] = c;
                
                if (groupPos == 4) {
                    // Decode this group
                    std::array<char, 3> decoded;
                    size_t decodedLen;
                    if (!Base64::DecodeToBuffer(std::string_view(group.data(), 4), std::span(decoded), decodedLen)) {
                        return false;
                    }
                    
                    // Copy to output
                    for (size_t j = 0; j < decodedLen && outputSize < output.size(); ++j) {
                        output[outputSize++] = decoded[j];
                    }
                    
                    groupPos = 0;
                }
            }
            
            // Handle remaining characters with padding
            if (groupPos > 0) {
                while (groupPos < 4) {
                    group[groupPos++] = '=';
                }
                
                std::array<char, 3> decoded;
                size_t decodedLen;
                if (!Base64::DecodeToBuffer(std::string_view(group.data(), 4), std::span(decoded), decodedLen)) {
                    return false;
                }
                
                // Copy to output
                for (size_t j = 0; j < decodedLen && outputSize < output.size(); ++j) {
                    output[outputSize++] = decoded[j];
                }
            }
            
            return true;
        }
    }

    static bool ParseClaims(std::string_view claimsJson, Claims& claims,
                           std::span<char> workBuffer1, std::span<char> workBuffer2,
                           std::span<char> workBuffer3, std::span<char> workBuffer4) {

        // Clear claims first
        claims = Claims{};
        
        // Parse string claims (need persistent storage)
        size_t issLen = 0, subLen = 0, audLen = 0, jtiLen = 0;
        
        bool hasIss = JSON::GetString(claimsJson, "iss", workBuffer1, issLen);
        bool hasSub = JSON::GetString(claimsJson, "sub", workBuffer2, subLen);
        bool hasAud = JSON::GetString(claimsJson, "aud", workBuffer3, audLen);
        bool hasJti = JSON::GetString(claimsJson, "jti", workBuffer4, jtiLen);
        
        // Parse numeric claims first (they don't need buffers)
        JSON::GetNumber(claimsJson, "exp", claims.exp);
        JSON::GetNumber(claimsJson, "nbf", claims.nbf);
        JSON::GetNumber(claimsJson, "iat", claims.iat);
        
        // DEBUG: Print what we parsed
        #ifdef JWT_DEBUG
        System::console << "JWT ParseClaims debug:" << Console::endl;
        System::console << "  claimsJson: " << claimsJson << Console::endl;
        System::console << "  iat parsed: " << Console::dec(claims.iat) << Console::endl;
        #endif
        
        // Now check for custom claim "name" - we'll try to use a buffer that's not in use
        size_t nameLen = 0;
        bool hasName = false;
        
        // Try to find an unused buffer for "name"
        if (!hasIss && JSON::GetString(claimsJson, "name", workBuffer1, nameLen)) {
            claims.AddCustomClaim("name", std::string_view(workBuffer1.data(), nameLen));
            hasName = true;
        } else if (!hasSub && JSON::GetString(claimsJson, "name", workBuffer2, nameLen)) {
            claims.AddCustomClaim("name", std::string_view(workBuffer2.data(), nameLen));
            hasName = true;
        } else if (!hasAud && JSON::GetString(claimsJson, "name", workBuffer3, nameLen)) {
            claims.AddCustomClaim("name", std::string_view(workBuffer3.data(), nameLen));
            hasName = true;
        } else if (!hasJti && JSON::GetString(claimsJson, "name", workBuffer4, nameLen)) {
            claims.AddCustomClaim("name", std::string_view(workBuffer4.data(), nameLen));
            hasName = true;
        }
        
        // Set standard claims after custom claims to avoid buffer conflicts
        if (hasIss) claims.iss = std::string_view(workBuffer1.data(), issLen);
        if (hasSub) claims.sub = std::string_view(workBuffer2.data(), subLen);
        if (hasAud) claims.aud = std::string_view(workBuffer3.data(), audLen);
        if (hasJti) claims.jti = std::string_view(workBuffer4.data(), jtiLen);

        return true;
    }

public:
    static bool CreateToken(
        std::span<char> tokenBuffer,
        size_t& tokenLength,
        const Header& header,
        const Claims& claims,
        std::string_view secret
    ) {

        std::array<char, 256> headerBuffer{};
        std::array<char, 512> claimsBuffer{};
        std::array<char, 128> encodedHeaderBuffer{};
        std::array<char, 256> encodedClaimsBuffer{};

        // Create JSON
        size_t headerLen, claimsLen;
        if (!CreateHeaderJson(std::span(headerBuffer), headerLen, header)) {
            return false;
        }

        if (!CreateClaimsJson(std::span(claimsBuffer), claimsLen, claims)) {
            return false;
        }

        // Encode header and claims
        size_t encodedHeaderLen = Base64UrlEncode(std::span(encodedHeaderBuffer),
                                                 std::string_view(headerBuffer.data(), headerLen));
        size_t encodedClaimsLen = Base64UrlEncode(std::span(encodedClaimsBuffer),
                                                 std::string_view(claimsBuffer.data(), claimsLen));

        if (encodedHeaderLen == 0 || encodedClaimsLen == 0) {
            return false;
        }

        // Build payload
        std::array<char, 512> payloadBuffer{};
        size_t payloadLen = 0;

        // Copy encoded header
        for (size_t i = 0; i < encodedHeaderLen && payloadLen < payloadBuffer.size(); ++i) {
            payloadBuffer[payloadLen++] = encodedHeaderBuffer[i];
        }

        // Add dot
        if (payloadLen < payloadBuffer.size()) {
            payloadBuffer[payloadLen++] = '.';
        }

        // Copy encoded claims
        for (size_t i = 0; i < encodedClaimsLen && payloadLen < payloadBuffer.size(); ++i) {
            payloadBuffer[payloadLen++] = encodedClaimsBuffer[i];
        }

        // Create signature
        std::array<uint8, 32> signature{};
        if (header.alg == Algorithm::HS256) {
            signature = HMAC<32>::Compute(
                std::span(reinterpret_cast<const uint8*>(secret.data()), secret.size()),
                std::span(reinterpret_cast<const uint8*>(payloadBuffer.data()), payloadLen)
            );
        } else {
            return false;
        }

        std::array<char, 64> encodedSignatureBuffer{};
        size_t encodedSignatureLen = Base64UrlEncode(std::span(encodedSignatureBuffer), std::span(signature));

        if (encodedSignatureLen == 0) {
            return false;
        }

        // Build final token
        tokenLength = 0;

        // Copy payload
        for (size_t i = 0; i < payloadLen && tokenLength < tokenBuffer.size(); ++i) {
            tokenBuffer[tokenLength++] = payloadBuffer[i];
        }

        // Add dot
        if (tokenLength < tokenBuffer.size()) {
            tokenBuffer[tokenLength++] = '.';
        }

        // Copy signature
        for (size_t i = 0; i < encodedSignatureLen && tokenLength < tokenBuffer.size(); ++i) {
            tokenBuffer[tokenLength++] = encodedSignatureBuffer[i];
        }

        return tokenLength > 0 && tokenLength < tokenBuffer.size();
    }

    static bool VerifyToken(
        std::string_view token,
        std::string_view secret,
        Header& header,
        Claims& claims,
        std::span<char> headerWorkBuffer,
        std::span<char> claimsWorkBuffer,
        std::span<char> claimsStringBuffer1,
        std::span<char> claimsStringBuffer2,
        std::span<char> claimsStringBuffer3,
        std::span<char> claimsStringBuffer4
    ) {

        // Find dots
        size_t firstDot = token.find('.');
        if (firstDot == std::string_view::npos) return false;

        size_t secondDot = token.find('.', firstDot + 1);
        if (secondDot == std::string_view::npos) return false;

        std::string_view encodedHeader = token.substr(0, firstDot);
        std::string_view encodedClaims = token.substr(firstDot + 1, secondDot - firstDot - 1);
        std::string_view encodedSignature = token.substr(secondDot + 1);

        // Decode header and claims
        size_t headerLen, claimsLen;

        if (!Base64UrlDecode(headerWorkBuffer, encodedHeader, headerLen)) return false;
        if (!Base64UrlDecode(claimsWorkBuffer, encodedClaims, claimsLen)) return false;

        std::string_view headerJson(headerWorkBuffer.data(), headerLen);
        std::string_view claimsJson(claimsWorkBuffer.data(), claimsLen);

        // Parse header
        std::array<char, 32> algBuffer{};
        std::array<char, 32> typBuffer{};
        size_t algLen, typLen;

        if (!JSON::GetString(headerJson, "alg", std::span(algBuffer), algLen)) {
            return false;
        }

        std::string_view algStr(algBuffer.data(), algLen);
        header.alg = StringToAlgorithm(algStr);

        if (JSON::GetString(headerJson, "typ", std::span(typBuffer), typLen)) {
            header.typ = std::string_view(typBuffer.data(), typLen);
        } else {
            header.typ = "JWT";
        }

        // Parse claims
        if (!ParseClaims(claimsJson, claims, claimsStringBuffer1, claimsStringBuffer2,
                        claimsStringBuffer3, claimsStringBuffer4)) {
            return false;
        }

        // Verify signature
        std::string_view payload = token.substr(0, secondDot);
        std::array<uint8, 32> expectedSignature{};

        if (header.alg == Algorithm::HS256) {
            expectedSignature = HMAC<32>::Compute(
                std::span(reinterpret_cast<const uint8*>(secret.data()), secret.size()),
                std::span(reinterpret_cast<const uint8*>(payload.data()), payload.size())
            );
        } else {
            return false;
        }

        std::array<char, 64> expectedEncodedBuffer{};
        size_t expectedEncodedLen = Base64UrlEncode(std::span(expectedEncodedBuffer), std::span(expectedSignature));

        if (expectedEncodedLen == 0) return false;

        std::string_view expectedEncoded(expectedEncodedBuffer.data(), expectedEncodedLen);

        return expectedEncoded == encodedSignature;
    }

    // Simplified verify function with automatic buffer allocation
    static bool VerifyToken(std::string_view token, std::string_view secret) {
        Header header;
        Claims claims;

        std::array<char, 256> headerWorkBuffer{};
        std::array<char, 512> claimsWorkBuffer{};
        std::array<char, 64> claimsBuffer1{};
        std::array<char, 64> claimsBuffer2{};
        std::array<char, 64> claimsBuffer3{};
        std::array<char, 64> claimsBuffer4{};

        return VerifyToken(
            token, secret, header, claims,
            std::span(headerWorkBuffer),
            std::span(claimsWorkBuffer),
            std::span(claimsBuffer1),
            std::span(claimsBuffer2),
            std::span(claimsBuffer3),
            std::span(claimsBuffer4)
        );
    }

    static bool IsTokenExpired(const Claims& claims, uint64 currentTime = 0) {
        // If no expiration set, token never expires
        if (claims.exp == 0) return false;
        
        // If no current time provided and no RTC available, can't check expiration
        if (currentTime == 0) return false;
        
        return currentTime >= claims.exp;
    }

    static bool IsTokenValid(const Claims& claims, uint64 currentTime = 0) {
        // If no current time provided and no RTC available, skip time-based validation
        if (currentTime == 0) return true;
        
        if (claims.nbf != 0 && currentTime < claims.nbf) return false;
        if (claims.exp != 0 && currentTime >= claims.exp) return false;

        return true;
    }

    // For embedded systems without RTC, return 0 to indicate no timestamp
    static uint64 GetCurrentTime() {
        return 0; // No real time clock available
    }

    static uint64 CreateExpirationTime(uint32 secondsFromNow) {
        uint64 currentTime = GetCurrentTime();
        if (currentTime == 0) return 0; // No timestamp available
        return currentTime + secondsFromNow;
    }
};

