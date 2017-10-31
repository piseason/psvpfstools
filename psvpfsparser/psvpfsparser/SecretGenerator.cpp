#include "SecretGenerator.h"

#include <stdint.h>
#include <string>

#include "sha1.h"
#include "aes.h"

#include "F00DKeyEncryptor.h"

//TODO: this is only part of the reversed code
//TODO: Crypto engine consists of multiple layers, where each layer should be placed into separate file

unsigned char hmac_key0[0x14] = {0xE4, 0x62, 0x25, 0x8B, 0x1F, 0x31, 0x21, 0x56, 0x07, 0x45, 0xDB, 0x62, 0xB1, 0x43, 0x67, 0x23, 0xD2, 0xBF, 0x80, 0xFE}; 

unsigned char hmac_key1[0x14] = {0xAF, 0xE6, 0x56, 0xBB, 0x3C, 0x17, 0x25, 0x6A, 0x3C, 0x80, 0x9F, 0x6E, 0x9B, 0xF1, 0x9F, 0xDD, 0x5A, 0x38, 0x85, 0x43};

unsigned char iv_21A93F0[0x10] = {0x74, 0xD2, 0x0C, 0xC3, 0x98, 0x81, 0xC2, 0x13, 0xEE, 0x77, 0x0B, 0x10, 0x10, 0xE4, 0xBE, 0xA7};

int SceKernelUtilsForDriver_ksceSha1Digest(const unsigned char *source, uint32_t size, unsigned char result[0x14])
{
   sha1(source, size, result);

   return 0;
}

int SceKernelUtilsForDriver_ksceHmacSha1Digest(const unsigned char* key, uint32_t key_len, const unsigned char* data, uint32_t data_len, unsigned char digest[0x14])
{
   sha1_hmac(key, key_len, data, data_len, digest);

   return 0;
}


int SceSblSsMgrForDriver_sceSblSsMgrAESCBCEncryptWithKeygenForDriver(const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, unsigned char* iv, uint16_t key_id, int mask_enable)
{
   F00DKeyEncryptor ec;
   unsigned char drv_key[0x20] = {0}; //use max possible buffer
   if(ec.encrypt_key(key, key_size, drv_key) < 0)
      return -1;

   aes_context aes_ctx;
   memset(&aes_ctx, 0, sizeof(aes_ctx));
   aes_setkey_enc(&aes_ctx, drv_key, key_size);
   aes_crypt_cbc(&aes_ctx, AES_ENCRYPT, size, iv, src,dst);

   return 0;
}

int SceSblSsMgrForDriver_sceSblSsMgrAESECBEncryptWithKeygenForDriver(const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, uint16_t key_id, int mask_enable)
{
   F00DKeyEncryptor ec;
   unsigned char drv_key[0x20] = {0}; //use max possible buffer
   if(ec.encrypt_key(key, key_size, drv_key) < 0)
      return -1;

   aes_context aes_ctx;
   memset(&aes_ctx, 0, sizeof(aes_ctx));
   aes_setkey_enc(&aes_ctx, drv_key, key_size);
   aes_crypt_ecb(&aes_ctx, AES_ENCRYPT, src, dst);

   return 0;
}


int sha1Digest(unsigned char *result, const unsigned char *source, int size)
{
   return SceKernelUtilsForDriver_ksceSha1Digest(source, size, result);
}

int hmacSha1Digest(unsigned char* digest, const unsigned char* key, const unsigned char* data, int data_len)
{
   return SceKernelUtilsForDriver_ksceHmacSha1Digest(key, 0x14, data, data_len, digest);
}


int AESCBCEncryptWithKeygen_base(const unsigned char* klicensee, unsigned char* iv, uint32_t size, const unsigned char* src, unsigned char* dst, uint16_t key_id)
{
   uint16_t kid = 0 - (key_id - 1) + (key_id - 1); // ???

   int size_tail = size & 0xF; // get size of tail
   int size_block = size & (~0xF); // get block size aligned to 0x10 boundary
   
   //encrypt N blocks of source data with klicensee and iv

   if(size_block != 0)
   {
      int result0 = SceSblSsMgrForDriver_sceSblSsMgrAESCBCEncryptWithKeygenForDriver(src, dst, size_block, klicensee, 0x80, iv, kid, 1);
      if(result0 != 0)
         return result0;  
   }

   //handle tail section - do a Cipher Text Stealing

   if(size_tail == 0)
      return 0;

   //align destination buffer

   unsigned char iv_enc[0x10] = {0};
   //unsigned char* iv_enc_aligned = iv_enc + ((0 - (int)iv_enc) & 0x3F);
   unsigned char* iv_enc_aligned = iv_enc;

   //encrypt iv using klicensee
     
   int result1 = SceSblSsMgrForDriver_sceSblSsMgrAESECBEncryptWithKeygenForDriver(iv, iv_enc_aligned, 0x10, klicensee, 0x80, kid, 1);
   if(result1 != 0)
      return result1;

   //produce destination tail by xoring source tail with encrypted iv

   for(int i = 0; i < size_tail; i++)
      dst[size_block + i] = src[size_block + i] ^ iv_enc_aligned[i];

   return 0;
}


int gen_secret(unsigned char* combo_aligned, uint32_t salt0, uint32_t salt1)
{
   int saltin0[1] = {0};
   int saltin1[2] = {0};
   unsigned char base[0x14] = {0};

   if(salt0 == 0)
   {
      saltin0[0] = salt1;
      hmacSha1Digest(base, hmac_key1, (unsigned char*)saltin0, 4); // derive base with one salt
   }
   else
   {
      saltin1[0] = salt0;
      saltin1[1] = salt1;
      hmacSha1Digest(base, hmac_key1, (unsigned char*)saltin1, 8); // derive base with two salts
   }

   memcpy(combo_aligned, base, 0x14); // calculated digest will be src data

   return 0;
}

int generate_secret_np(unsigned char* secret, const unsigned char* klicensee, uint32_t salt0, uint32_t salt1, uint16_t key_id)
{
   unsigned char drvkey[0x14] = {0};
   unsigned char iv[0x10] = {0};
   unsigned char combo[0x14] = {0};

   //align buffers

   /*
   unsigned char* drvkey_aligned = drvkey + ((0 - (int)drvkey) & 0x3F);
   unsigned char* iv_aligned = iv + ((0 - (int)iv) & 0x3F);
   unsigned char* combo_aligned = combo + ((0 - (int)combo) & 0x3F);
   */

   unsigned char* drvkey_aligned = drvkey;
   unsigned char* iv_aligned = iv;
   unsigned char* combo_aligned = combo;

   gen_secret(combo_aligned, salt0, salt1);

   memcpy(iv_aligned, iv_21A93F0, 0x10); //initialize iv

   AESCBCEncryptWithKeygen_base(klicensee, iv_aligned, 0x14, combo_aligned, drvkey_aligned, key_id);

   memcpy(secret, drvkey_aligned, 0x14); // copy derived key
   
   return 0;
}

int generate_secret(unsigned char* secret, const unsigned char* klicensee,  uint32_t salt1)
{
   int saltin[2] = {0};
   unsigned char base0[0x14] = {0};
   unsigned char base1[0x14] = {0};
   unsigned char combo[0x28] = {0};
   unsigned char drvkey[0x14] = {0};

   sha1Digest(base0, klicensee, 0x10); // calculate digest of klicensee

   saltin[0] = 0xA;
   saltin[1] = salt1;

   sha1Digest(base1, (unsigned char*)saltin, 8); // calculate digest of salt

   memcpy(combo, base0, 0x14);
   memcpy(combo + 0x14, base1, 0x14);
         
   sha1Digest(drvkey, combo, 0x28); // calculate digest of combination of klicensee and salt digests
               
   memcpy(secret, drvkey, 0x14); // copy derived key

   return 0;
}


int scePfsUtilGetSecret(unsigned char* secret, const unsigned char* klicensee, uint32_t salt0, uint16_t flag, uint32_t salt1, uint16_t key_id)
{
   if((flag & 1) > 0) // check bit 0
   {
      memset(secret, 0, 0x14);
      return 0;
   }

   if((flag & 2) > 0) // check bit 1
   {
      generate_secret_np(secret, klicensee, salt0, salt1, key_id);
      return 0;
   }

   return generate_secret(secret, klicensee, salt1);
}