/**
 * @file libcomp/src/Decrypt.cpp
 * @ingroup libcomp
 *
 * @author COMP Omega <compomega@tutanota.com>
 *
 * @brief Encryption and decryption function implementations.
 *
 * This file is part of the COMP_hack Library (libcomp).
 *
 * Copyright (C) 2012-2016 COMP_hack Team <compomega@tutanota.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Decrypt.h"
#include "Config.h"
#include "Exception.h"
/// @todo Uncomment when the Packet class is included.
//#include "Packet.h"

#include <openssl/crypto.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/bn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif // _WIN32

#include <fstream>
#include <iomanip>
#include <sstream>
#include <cassert>

using namespace libcomp;

/// Blowfish key for file encryption.
static BF_KEY gFileEncryptionKey;

/**
 * @brief Setup the Blowfish key when the application starts.
 */
static __attribute__((constructor)) void InitDecrypt()
{
    // Sanity check the configuration.
    assert(4  == strlen(Config::ENCRYPTED_FILE_MAGIC));
    assert(16 == strlen(Config::ENCRYPTED_FILE_KEY));
    assert(8  == strlen(Config::ENCRYPTED_FILE_IV));

    BF_set_key(&gFileEncryptionKey, 16, reinterpret_cast<const unsigned char*>(
        Config::ENCRYPTED_FILE_KEY));
}

/**
 * Header for an encrypted file.
 */
typedef struct __attribute__((packed))
{
    /// Magic to identify the file type.
    char magic[4];

    /// Size (in bytes) of the file after decryption.
    uint32_t originalSize;
} EncryptedFileHeader_t;

std::vector<char> Decrypt::DecryptFile(const std::string& path)
{
    std::vector<char> data = Decrypt::LoadFile(path);

    // Check the file is large enough.
    if(sizeof(EncryptedFileHeader_t) < data.size())
    {
        EncryptedFileHeader_t *pHeader = reinterpret_cast<
            EncryptedFileHeader_t*>(&data[0]);

        // Check the header.
        if(data.size() >= (sizeof(EncryptedFileHeader_t) +
            pHeader->originalSize) && 0 == memcmp(&pHeader->magic[0],
            Config::ENCRYPTED_FILE_MAGIC, sizeof(pHeader->magic)))
        {
            uint32_t originalSize = pHeader->originalSize;
            pHeader = nullptr;

            // Remove the header.
            data.erase(data.begin(), data.begin() +
                sizeof(EncryptedFileHeader_t));

            // Decrypt the file.
            Decrypt::DecryptCbc(data, originalSize);
        }
        else
        {
            pHeader = nullptr;
            data.clear();
        }
    }

    return data;
}

bool Decrypt::EncryptFile(const std::string& path,
    const std::vector<char>& data)
{
    std::vector<char> dataCopy = data;

    EncryptedFileHeader_t header;
    header.originalSize = static_cast<uint32_t>(data.size());

    memcpy(&header.magic[0], Config::ENCRYPTED_FILE_MAGIC,
        sizeof(header.magic));

    Decrypt::EncryptCbc(dataCopy);

    std::ofstream out;
    out.open(path, std::ofstream::out | std::ofstream::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(&dataCopy[0], dataCopy.size());

    return out.good();
}

std::vector<char> Decrypt::LoadFile(const std::string& path, int requestedSize)
{
    std::ifstream::streampos fileSize;
    std::vector<char> data;
    std::ifstream file;

    if(0 < requestedSize)
    {
        file.open(path.c_str(), std::ifstream::in | std::ifstream::binary);
        fileSize = static_cast<std::ifstream::streampos>(requestedSize);

        if(file.good() && 0 < fileSize)
        {
            data.resize(fileSize);
            file.read(&data[0], fileSize);
        }
    }
    else
    {
        file.open(path.c_str(), std::ifstream::in | std::ifstream::binary |
            std::ifstream::ate);
        fileSize = file.tellg();
        file.seekg(0);

        if(file.good() && 0 < fileSize)
        {
            try
            {
                data.reserve(fileSize);
                data.assign(std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>());
            }
            catch(std::bad_alloc)
            {
                data.clear();
            }

        }
    }

    if(!file.good() || data.size() != fileSize)
    {
        data.clear();
    }

    return data;
}

String Decrypt::GenerateRandom(int sz)
{
    // Check for an odd size.
    if(0 < sz && 0 != (sz % 2))
    {
        EXCEPTION(String("Odd size detected in call to GenerateRandom()"));

        return String();
    }

    // If no size was passed in, assume 80 digits; otherwise, divide the size
    // by 2 to obtain how many bytes are required.
    sz = sz <= 0 ? 40 : (sz >> 1);

    // Where to store the random data.
    std::vector<char> random;

#ifdef WIN32
    HCRYPTPROV hCryptProv;

    PBYTE pbData = new BYTE[sz];

    if(nullptr == pbData)
    {
        EXCEPTION(tr("Failed to allocate pbData"));

        return String();
    }

    // On Windows, use the cryto API to generate the random data. Acquire a
    // context to generate the random data with.
    if(TRUE != CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL,
        CRYPT_VERIFYCONTEXT | CRYPT_NEWKEYSET))
    {
        delete[] pbData;

        EXCEPTION(tr("CryptAcquireContext: %1").arg(getLastErrorString()));

        return String();
    }

    // Generate the random data.
    if(TRUE != CryptGenRandom(hCryptProv, sz, pbData))
    {
        delete[] pbData;

        EXCEPTION(tr("CryptGenRandom: %1").arg(getLastErrorString()));

        return String();
    }

    // Release the context.
    if(TRUE != CryptReleaseContext(hCryptProv, 0))
    {
        delete[] pbData;

        EXCEPTION(tr("CryptReleaseContext: %1").arg(getLastErrorString()));

        return String();
    }

    // Convert the raw data to a QByteArray.
    random = std::move(std::vector<char*>(reinterpret_cast<char*>(
        pbData), sz));
#else // WIN32
    // On Linux, use /dev/urandom.
    random = LoadFile("/dev/urandom", sz);

    // Check that enough data was read.
    if(random.size() != sz)
    {
        EXCEPTION("Failed to read from /dev/urandom");

        return String();
    }
#endif // WIN32

    std::stringstream ss;

    // Convert the bytes into a base-16 string.
    for(char byte : random)
    {
        ss << std::hex << std::setw(2) << std::setfill('0')
            << ((int)byte & 0xFF);
    }

#ifdef WIN32
    // After conversion this buffer isn't needed.
    delete[] pbData;
#endif // WIN32

    return ss.str();
}

uint32_t Decrypt::GenerateSessionKey()
{
    uint32_t sessionKey = 0;

#ifdef WIN32
    HCRYPTPROV hCryptProv;

    // On Windows, use the cryto API to generate the random data. Acquire a
    // context to generate the random data with.
    if(TRUE != CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, 0))
    {
        return 0;
    }

    // Generate the random data.
    if(TRUE != CryptGenRandom(hCryptProv, 4, reinterpret_cast<PBYTE>(
        &sessionKey)))
    {
        return 0;
    }

    // Release the context.
    if(TRUE != CryptReleaseContext(hCryptProv, 0))
    {
        return 0;
    }
#else // WIN32
    // On Linux, use /dev/urandom.
    std::vector<char> data = LoadFile("/dev/urandom", sizeof(sessionKey));

    // Sanity check the size of the random data.
    if(sizeof(sessionKey) != data.size())
    {
        return 0;
    }

    // Copy the session key.
    sessionKey = *reinterpret_cast<uint32_t*>(&data[0]);
#endif // WIN32

    // Ensure the session key won't be interpreted as a negative number.
    sessionKey &= 0x7FFFFFFF;

    // For the unlikely situation the random value is zero.
    if(0 == sessionKey)
    {
        sessionKey = 0x8BADF00D;
    }

    return sessionKey;
}

String Decrypt::GenDiffieHellman(const String& g, const String& p,
    const String& a, int outputSize)
{
    BIGNUM *pBase = nullptr, *pPrime = nullptr, *pSecret = nullptr;

    // Convert each argument from a base-16 string to a bignum object.
    if(0 >= BN_hex2bn(&pBase, g.C()) || nullptr == pBase)
    {
        BN_clear_free(pBase);

        return String();
    }

    if(0 >= BN_hex2bn(&pPrime, p.C()) || nullptr == pPrime)
    {
        BN_clear_free(pBase);
        BN_clear_free(pPrime);

        return String();
    }

    if(0 >= BN_hex2bn(&pSecret, a.C()) || nullptr == pSecret)
    {
        BN_clear_free(pBase);
        BN_clear_free(pPrime);
        BN_clear_free(pSecret);

        return String();
    }

    // Create a context.
    BN_CTX *pCtx = BN_CTX_new();

    if(nullptr == pCtx)
    {
        BN_clear_free(pBase);
        BN_clear_free(pPrime);
        BN_clear_free(pSecret);

        return String();
    }

    // Allocate a bignum object to store the result in.
    BIGNUM *pResult = BN_new();

    if(nullptr == pResult)
    {
        BN_clear_free(pBase);
        BN_clear_free(pPrime);
        BN_clear_free(pSecret);
        BN_CTX_free(pCtx);

        return String();
    }

    // Clear the value first (this might not be needed).
    BN_clear(pResult);

    // Peform the operation on the value.
    if(1 != BN_mod_exp(pResult, pBase, pSecret, pPrime, pCtx))
    {
        BN_clear_free(pResult);
        BN_clear_free(pBase);
        BN_clear_free(pPrime);
        BN_clear_free(pSecret);
        BN_CTX_free(pCtx);

        return String();
    }

    // Free the context and arguments after use.
    BN_clear_free(pBase);
    BN_clear_free(pPrime);
    BN_clear_free(pSecret);
    BN_CTX_free(pCtx);

    // Convert the result to a base-16 string.
    char *pHexResult = BN_bn2hex(pResult);

    if(nullptr == pHexResult)
    {
        BN_clear_free(pResult);

        return String();
    }

    // Convert the base-16 string to a QString.
    String result = pHexResult;

    // If a specific output size was specified, pad the output to that size.
    if(0 < outputSize)
    {
        result = result.RightJustified(outputSize, '0');
    }

    // We no longer need the converted string so free it.
    OPENSSL_free(pHexResult);

    // We no longer need the result.
    BN_clear_free(pResult);

    // Return the final result.
    return result;
}

void Decrypt::Encrypt(const BF_KEY& key, std::vector<char>& data)
{
    std::vector<char>::size_type size = data.size();

    // Make room for the padded block.
    if(0 != (size % BLOWFISH_BLOCK_SIZE))
    {
        // Round up to a multiple of the block size.
        size = ((size + BLOWFISH_BLOCK_SIZE - 1) /
            BLOWFISH_BLOCK_SIZE) * BLOWFISH_BLOCK_SIZE;

        // Resize the data vector.
        data.resize(size, 0);
    }

    char *pData = &data[0];

    // Encrypt each full block.
    while(BLOWFISH_BLOCK_SIZE <= size)
    {
        BF_encrypt(reinterpret_cast<BF_LONG*>(pData), &key);
        pData += BLOWFISH_BLOCK_SIZE;
        size -= BLOWFISH_BLOCK_SIZE;
    }
}

void Decrypt::Encrypt(std::vector<char>& data)
{
    Encrypt(gFileEncryptionKey, data);
}

void Decrypt::Decrypt(const BF_KEY& key, std::vector<char>& data,
    std::vector<char>::size_type realSize)
{
    std::vector<char>::size_type size = data.size();
    char *pData = &data[0];

    if((0 == realSize || realSize <= size) &&
        0 == (size % BLOWFISH_BLOCK_SIZE))
    {
        // Decrypt each full block.
        while(BLOWFISH_BLOCK_SIZE <= size)
        {
            BF_decrypt(reinterpret_cast<BF_LONG*>(pData), &key);
            pData += BLOWFISH_BLOCK_SIZE;
            size -= BLOWFISH_BLOCK_SIZE;
        }
    }

    // Resize the data if requested.
    if(0 != realSize)
    {
        data.resize(realSize);
    }
}

void Decrypt::Decrypt(std::vector<char>& data,
    std::vector<char>::size_type realSize)
{
    Decrypt(gFileEncryptionKey, data, realSize);
}

void Decrypt::EncryptCbc(const BF_KEY& key, uint64_t& initializationVector,
    std::vector<char>& data)
{
    std::vector<char>::size_type size = data.size();
    uint64_t previousBlock = initializationVector;

    // Make room for the padded block.
    if(0 != (size % BLOWFISH_BLOCK_SIZE))
    {
        // Round up to a multiple of the block size.
        size = ((size + BLOWFISH_BLOCK_SIZE - 1) /
            BLOWFISH_BLOCK_SIZE) * BLOWFISH_BLOCK_SIZE;

        // Resize the data vector.
        data.resize(size, 0);
    }

    char *pData = &data[0];

    // Encrypt each full block.
    while(BLOWFISH_BLOCK_SIZE <= size)
    {
        uint64_t unencryptedBlock = *reinterpret_cast<uint64_t*>(pData);
        uint64_t encryptedBlock = unencryptedBlock ^ previousBlock;

        BF_encrypt(reinterpret_cast<BF_LONG*>(&encryptedBlock), &key);

        // Save the data back into the vector.
        *reinterpret_cast<uint64_t*>(pData) = encryptedBlock;

        pData += BLOWFISH_BLOCK_SIZE;
        size -= BLOWFISH_BLOCK_SIZE;

        // Save this for the next round.
        previousBlock = encryptedBlock;
    }

    // Save the vector used so one may call this function again.
    initializationVector = previousBlock;
}

void Decrypt::EncryptCbc(std::vector<char>& data)
{
    uint64_t initializationVector = *reinterpret_cast<const uint64_t*>(
        Config::ENCRYPTED_FILE_IV);

    EncryptCbc(gFileEncryptionKey, initializationVector, data);
}

void Decrypt::DecryptCbc(const BF_KEY& key, uint64_t& initializationVector,
    std::vector<char>& data, std::vector<char>::size_type realSize)
{
    std::vector<char>::size_type size = data.size();
    uint64_t previousBlock = initializationVector;
    char *pData = &data[0];

    if((0 == realSize || realSize <= size) &&
        0 == (size % BLOWFISH_BLOCK_SIZE))
    {
        // Decrypt each full block.
        while(BLOWFISH_BLOCK_SIZE <= size)
        {
            uint64_t encryptedBlock = *reinterpret_cast<uint64_t*>(pData);
            uint64_t unencryptedBlock = encryptedBlock;

            BF_decrypt(reinterpret_cast<BF_LONG*>(&unencryptedBlock), &key);

            unencryptedBlock ^= previousBlock;

            // Save the data back into the vector.
            *reinterpret_cast<uint64_t*>(pData) = unencryptedBlock;

            pData += BLOWFISH_BLOCK_SIZE;
            size -= BLOWFISH_BLOCK_SIZE;

            // Save this for the next round.
            previousBlock = encryptedBlock;
        }
    }

    // Resize the data if requested.
    if(0 != realSize)
    {
        data.resize(realSize);
    }

    // Save the vector used so one may call this function again.
    initializationVector = previousBlock;
}

void Decrypt::DecryptCbc(std::vector<char>& data,
    std::vector<char>::size_type realSize)
{
    uint64_t initializationVector = *reinterpret_cast<const uint64_t*>(
        Config::ENCRYPTED_FILE_IV);

    DecryptCbc(gFileEncryptionKey, initializationVector, data, realSize);
}

#if 0
/// @todo Uncomment when the Packet class is included.
void Decrypt::EncryptPacket(const BF_KEY& key, Packet& p)
{
    // Skip over the sizes.
    p.seek(BLOWFISH_BLOCK_SIZE);

    // Buffer to store the current block of data.
    BF_LONG buffer[BLOWFISH_BLOCK_SIZE / sizeof(BF_LONG)];

    // Encrypt each block of data.
    for(uint32_t i = BLOWFISH_BLOCK_SIZE; i < p.size();
        i += BLOWFISH_BLOCK_SIZE)
    {
        // Read the unencrypted data from the packet into the buffer.
        p.readArray(buffer, BLOWFISH_BLOCK_SIZE);

        // Move the packet pointer back.
        p.rewind(BLOWFISH_BLOCK_SIZE);

        // Encrypt the data in the buffer.
        BF_encrypt(buffer, &key);

        // Write the encrypted data back into the packet.
        p.writeArray(buffer, BLOWFISH_BLOCK_SIZE);
    }

    // Seek back to the beginning of the packet.
    p.rewind();
}

void Decrypt::DecryptPacket(const BF_KEY& key, Packet& p)
{
    // Skip over the sizes.
    p.seek(BLOWFISH_BLOCK_SIZE);

    // Buffer to store the current block of data.
    BF_LONG buffer[BLOWFISH_BLOCK_SIZE / sizeof(BF_LONG)];

    // Decrypt each block of data.
    for(uint32_t i = BLOWFISH_BLOCK_SIZE; i < p.size();
        i += BLOWFISH_BLOCK_SIZE)
    {
        // Read the encrypted data from the packet into the buffer.
        p.readArray(buffer, BLOWFISH_BLOCK_SIZE);

        // Move the packet pointer back.
        p.rewind(BLOWFISH_BLOCK_SIZE);

        // Decrypt the data in the buffer.
        BF_decrypt(buffer, &key);

        // Write the unencrypted data back into the packet.
        p.writeArray(buffer, BLOWFISH_BLOCK_SIZE);
    }

    // Seek back to the beginning of the packet.
    p.rewind();
}
#endif
