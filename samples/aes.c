#include "common.h"

__attribute__((annotate("secret"))) volatile int *rk;

#ifdef AES_LONG
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif
typedef unsigned short u16;
typedef unsigned char u8;

static void prefetch256(const void *table) {
    volatile unsigned long *t = (unsigned long *)table, ret;
    unsigned long sum;
    int i;

    /* 32 is common least cache-line size */
    for (sum = 0, i = 0; i < 256 / sizeof(t[0]); i += 32 / sizeof(t[0]))
        sum ^= t[i];

    ret = sum;
}

#undef GETU32
#define GETU32(p) (*((u32 *)(p)))

#if (defined(_WIN32) || defined(_WIN64)) && !defined(__MINGW32__)
typedef unsigned __int64 u64;
#define U64(C) C##UI64
#elif defined(__arch64__)
typedef unsigned long u64;
#define U64(C) C##UL
#else
typedef unsigned long long u64;
#define U64(C) C##ULL
#endif

/*-
Te [x] = S [x].[02, 01, 01, 03, 02, 01, 01, 03];
Te0[x] = S [x].[02, 01, 01, 03];
Te1[x] = S [x].[03, 02, 01, 01];
Te2[x] = S [x].[01, 03, 02, 01];
Te3[x] = S [x].[01, 01, 03, 02];
*/
#define Te0 (u32)((u64 *)((u8 *)Te + 0))
#define Te1 (u32)((u64 *)((u8 *)Te + 3))
#define Te2 (u32)((u64 *)((u8 *)Te + 2))
#define Te3 (u32)((u64 *)((u8 *)Te + 1))

static const u64 Te[256] = {
    U64(0xa56363c6a56363c6), U64(0x847c7cf8847c7cf8),
    U64(0x997777ee997777ee), U64(0x8d7b7bf68d7b7bf6),
    U64(0x0df2f2ff0df2f2ff), U64(0xbd6b6bd6bd6b6bd6),
    U64(0xb16f6fdeb16f6fde), U64(0x54c5c59154c5c591),
    U64(0x5030306050303060), U64(0x0301010203010102),
    U64(0xa96767cea96767ce), U64(0x7d2b2b567d2b2b56),
    U64(0x19fefee719fefee7), U64(0x62d7d7b562d7d7b5),
    U64(0xe6abab4de6abab4d), U64(0x9a7676ec9a7676ec),
    U64(0x45caca8f45caca8f), U64(0x9d82821f9d82821f),
    U64(0x40c9c98940c9c989), U64(0x877d7dfa877d7dfa),
    U64(0x15fafaef15fafaef), U64(0xeb5959b2eb5959b2),
    U64(0xc947478ec947478e), U64(0x0bf0f0fb0bf0f0fb),
    U64(0xecadad41ecadad41), U64(0x67d4d4b367d4d4b3),
    U64(0xfda2a25ffda2a25f), U64(0xeaafaf45eaafaf45),
    U64(0xbf9c9c23bf9c9c23), U64(0xf7a4a453f7a4a453),
    U64(0x967272e4967272e4), U64(0x5bc0c09b5bc0c09b),
    U64(0xc2b7b775c2b7b775), U64(0x1cfdfde11cfdfde1),
    U64(0xae93933dae93933d), U64(0x6a26264c6a26264c),
    U64(0x5a36366c5a36366c), U64(0x413f3f7e413f3f7e),
    U64(0x02f7f7f502f7f7f5), U64(0x4fcccc834fcccc83),
    U64(0x5c3434685c343468), U64(0xf4a5a551f4a5a551),
    U64(0x34e5e5d134e5e5d1), U64(0x08f1f1f908f1f1f9),
    U64(0x937171e2937171e2), U64(0x73d8d8ab73d8d8ab),
    U64(0x5331316253313162), U64(0x3f15152a3f15152a),
    U64(0x0c0404080c040408), U64(0x52c7c79552c7c795),
    U64(0x6523234665232346), U64(0x5ec3c39d5ec3c39d),
    U64(0x2818183028181830), U64(0xa1969637a1969637),
    U64(0x0f05050a0f05050a), U64(0xb59a9a2fb59a9a2f),
    U64(0x0907070e0907070e), U64(0x3612122436121224),
    U64(0x9b80801b9b80801b), U64(0x3de2e2df3de2e2df),
    U64(0x26ebebcd26ebebcd), U64(0x6927274e6927274e),
    U64(0xcdb2b27fcdb2b27f), U64(0x9f7575ea9f7575ea),
    U64(0x1b0909121b090912), U64(0x9e83831d9e83831d),
    U64(0x742c2c58742c2c58), U64(0x2e1a1a342e1a1a34),
    U64(0x2d1b1b362d1b1b36), U64(0xb26e6edcb26e6edc),
    U64(0xee5a5ab4ee5a5ab4), U64(0xfba0a05bfba0a05b),
    U64(0xf65252a4f65252a4), U64(0x4d3b3b764d3b3b76),
    U64(0x61d6d6b761d6d6b7), U64(0xceb3b37dceb3b37d),
    U64(0x7b2929527b292952), U64(0x3ee3e3dd3ee3e3dd),
    U64(0x712f2f5e712f2f5e), U64(0x9784841397848413),
    U64(0xf55353a6f55353a6), U64(0x68d1d1b968d1d1b9),
    U64(0x0000000000000000), U64(0x2cededc12cededc1),
    U64(0x6020204060202040), U64(0x1ffcfce31ffcfce3),
    U64(0xc8b1b179c8b1b179), U64(0xed5b5bb6ed5b5bb6),
    U64(0xbe6a6ad4be6a6ad4), U64(0x46cbcb8d46cbcb8d),
    U64(0xd9bebe67d9bebe67), U64(0x4b3939724b393972),
    U64(0xde4a4a94de4a4a94), U64(0xd44c4c98d44c4c98),
    U64(0xe85858b0e85858b0), U64(0x4acfcf854acfcf85),
    U64(0x6bd0d0bb6bd0d0bb), U64(0x2aefefc52aefefc5),
    U64(0xe5aaaa4fe5aaaa4f), U64(0x16fbfbed16fbfbed),
    U64(0xc5434386c5434386), U64(0xd74d4d9ad74d4d9a),
    U64(0x5533336655333366), U64(0x9485851194858511),
    U64(0xcf45458acf45458a), U64(0x10f9f9e910f9f9e9),
    U64(0x0602020406020204), U64(0x817f7ffe817f7ffe),
    U64(0xf05050a0f05050a0), U64(0x443c3c78443c3c78),
    U64(0xba9f9f25ba9f9f25), U64(0xe3a8a84be3a8a84b),
    U64(0xf35151a2f35151a2), U64(0xfea3a35dfea3a35d),
    U64(0xc0404080c0404080), U64(0x8a8f8f058a8f8f05),
    U64(0xad92923fad92923f), U64(0xbc9d9d21bc9d9d21),
    U64(0x4838387048383870), U64(0x04f5f5f104f5f5f1),
    U64(0xdfbcbc63dfbcbc63), U64(0xc1b6b677c1b6b677),
    U64(0x75dadaaf75dadaaf), U64(0x6321214263212142),
    U64(0x3010102030101020), U64(0x1affffe51affffe5),
    U64(0x0ef3f3fd0ef3f3fd), U64(0x6dd2d2bf6dd2d2bf),
    U64(0x4ccdcd814ccdcd81), U64(0x140c0c18140c0c18),
    U64(0x3513132635131326), U64(0x2fececc32fececc3),
    U64(0xe15f5fbee15f5fbe), U64(0xa2979735a2979735),
    U64(0xcc444488cc444488), U64(0x3917172e3917172e),
    U64(0x57c4c49357c4c493), U64(0xf2a7a755f2a7a755),
    U64(0x827e7efc827e7efc), U64(0x473d3d7a473d3d7a),
    U64(0xac6464c8ac6464c8), U64(0xe75d5dbae75d5dba),
    U64(0x2b1919322b191932), U64(0x957373e6957373e6),
    U64(0xa06060c0a06060c0), U64(0x9881811998818119),
    U64(0xd14f4f9ed14f4f9e), U64(0x7fdcdca37fdcdca3),
    U64(0x6622224466222244), U64(0x7e2a2a547e2a2a54),
    U64(0xab90903bab90903b), U64(0x8388880b8388880b),
    U64(0xca46468cca46468c), U64(0x29eeeec729eeeec7),
    U64(0xd3b8b86bd3b8b86b), U64(0x3c1414283c141428),
    U64(0x79dedea779dedea7), U64(0xe25e5ebce25e5ebc),
    U64(0x1d0b0b161d0b0b16), U64(0x76dbdbad76dbdbad),
    U64(0x3be0e0db3be0e0db), U64(0x5632326456323264),
    U64(0x4e3a3a744e3a3a74), U64(0x1e0a0a141e0a0a14),
    U64(0xdb494992db494992), U64(0x0a06060c0a06060c),
    U64(0x6c2424486c242448), U64(0xe45c5cb8e45c5cb8),
    U64(0x5dc2c29f5dc2c29f), U64(0x6ed3d3bd6ed3d3bd),
    U64(0xefacac43efacac43), U64(0xa66262c4a66262c4),
    U64(0xa8919139a8919139), U64(0xa4959531a4959531),
    U64(0x37e4e4d337e4e4d3), U64(0x8b7979f28b7979f2),
    U64(0x32e7e7d532e7e7d5), U64(0x43c8c88b43c8c88b),
    U64(0x5937376e5937376e), U64(0xb76d6ddab76d6dda),
    U64(0x8c8d8d018c8d8d01), U64(0x64d5d5b164d5d5b1),
    U64(0xd24e4e9cd24e4e9c), U64(0xe0a9a949e0a9a949),
    U64(0xb46c6cd8b46c6cd8), U64(0xfa5656acfa5656ac),
    U64(0x07f4f4f307f4f4f3), U64(0x25eaeacf25eaeacf),
    U64(0xaf6565caaf6565ca), U64(0x8e7a7af48e7a7af4),
    U64(0xe9aeae47e9aeae47), U64(0x1808081018080810),
    U64(0xd5baba6fd5baba6f), U64(0x887878f0887878f0),
    U64(0x6f25254a6f25254a), U64(0x722e2e5c722e2e5c),
    U64(0x241c1c38241c1c38), U64(0xf1a6a657f1a6a657),
    U64(0xc7b4b473c7b4b473), U64(0x51c6c69751c6c697),
    U64(0x23e8e8cb23e8e8cb), U64(0x7cdddda17cdddda1),
    U64(0x9c7474e89c7474e8), U64(0x211f1f3e211f1f3e),
    U64(0xdd4b4b96dd4b4b96), U64(0xdcbdbd61dcbdbd61),
    U64(0x868b8b0d868b8b0d), U64(0x858a8a0f858a8a0f),
    U64(0x907070e0907070e0), U64(0x423e3e7c423e3e7c),
    U64(0xc4b5b571c4b5b571), U64(0xaa6666ccaa6666cc),
    U64(0xd8484890d8484890), U64(0x0503030605030306),
    U64(0x01f6f6f701f6f6f7), U64(0x120e0e1c120e0e1c),
    U64(0xa36161c2a36161c2), U64(0x5f35356a5f35356a),
    U64(0xf95757aef95757ae), U64(0xd0b9b969d0b9b969),
    U64(0x9186861791868617), U64(0x58c1c19958c1c199),
    U64(0x271d1d3a271d1d3a), U64(0xb99e9e27b99e9e27),
    U64(0x38e1e1d938e1e1d9), U64(0x13f8f8eb13f8f8eb),
    U64(0xb398982bb398982b), U64(0x3311112233111122),
    U64(0xbb6969d2bb6969d2), U64(0x70d9d9a970d9d9a9),
    U64(0x898e8e07898e8e07), U64(0xa7949433a7949433),
    U64(0xb69b9b2db69b9b2d), U64(0x221e1e3c221e1e3c),
    U64(0x9287871592878715), U64(0x20e9e9c920e9e9c9),
    U64(0x49cece8749cece87), U64(0xff5555aaff5555aa),
    U64(0x7828285078282850), U64(0x7adfdfa57adfdfa5),
    U64(0x8f8c8c038f8c8c03), U64(0xf8a1a159f8a1a159),
    U64(0x8089890980898909), U64(0x170d0d1a170d0d1a),
    U64(0xdabfbf65dabfbf65), U64(0x31e6e6d731e6e6d7),
    U64(0xc6424284c6424284), U64(0xb86868d0b86868d0),
    U64(0xc3414182c3414182), U64(0xb0999929b0999929),
    U64(0x772d2d5a772d2d5a), U64(0x110f0f1e110f0f1e),
    U64(0xcbb0b07bcbb0b07b), U64(0xfc5454a8fc5454a8),
    U64(0xd6bbbb6dd6bbbb6d), U64(0x3a16162c3a16162c)};

/*
 * Encrypt a single block
 * in and out can overlap
 */
// void AES_encrypt(const char *in, char *out, const AES_KEY *key)
void AES_encrypt(const char *in, char *out) {

    // const u32 *rk;
    u32 s0, s1, s2, s3, t[4];
    int r;

    // assert(in && out && key);
    // rk = rd_key;

    /*
     * map byte array block to cipher state
     * and add initial round key:
     */
    s0 = GETU32(in) ^ rk[0];
    s1 = GETU32(in + 4) ^ rk[1];
    s2 = GETU32(in + 8) ^ rk[2];
    s3 = GETU32(in + 12) ^ rk[3];

    /*
    printf("s: %d ", s0);
    printf("s: %d ", s1);
    printf("s: %d ", s2);
    printf("s: %d ", s3);
    printf("\n");*/

    t[0] = Te0[(s0)&0xff] ^
           Te1[(s1 >> 8) & 0xff] ^
           Te2[(s2 >> 16) & 0xff] ^
           Te3[(s3 >> 24)] ^
           rk[4];
    t[1] = Te0[(s1)&0xff] ^
           Te1[(s2 >> 8) & 0xff] ^
           Te2[(s3 >> 16) & 0xff] ^
           Te3[(s0 >> 24)] ^
           rk[5];
    t[2] = Te0[(s2)&0xff] ^
           Te1[(s3 >> 8) & 0xff] ^
           Te2[(s0 >> 16) & 0xff] ^
           Te3[(s1 >> 24)] ^
           rk[6];
    t[3] = Te0[(s3)&0xff] ^
           Te1[(s0 >> 8) & 0xff] ^
           Te2[(s1 >> 16) & 0xff] ^
           Te3[(s2 >> 24)] ^
           rk[7];

    s0 = t[0];
    s1 = t[1];
    s2 = t[2];
    s3 = t[3];

    /*
     * Nr - 2 full rounds:
     */
    for (rk += 8, r = 8; r > 0; rk += 4, r--) {
        // SENSITIVE: secret-dependent values (s1, s2, ...) as indices to access the T-table
        t[0] = Te0[(s0)&0xff] ^
               Te1[(s1 >> 8) & 0xff] ^
               Te2[(s2 >> 16) & 0xff] ^
               Te3[(s3 >> 24)] ^
               rk[0];
        t[1] = Te0[(s1)&0xff] ^
               Te1[(s2 >> 8) & 0xff] ^
               Te2[(s3 >> 16) & 0xff] ^
               Te3[(s0 >> 24)] ^
               rk[1];
        t[2] = Te0[(s2)&0xff] ^
               Te1[(s3 >> 8) & 0xff] ^
               Te2[(s0 >> 16) & 0xff] ^
               Te3[(s1 >> 24)] ^
               rk[2];
        t[3] = Te0[(s3)&0xff] ^
               Te1[(s0 >> 8) & 0xff] ^
               Te2[(s1 >> 16) & 0xff] ^
               Te3[(s2 >> 24)] ^
               rk[3];

        s0 = t[0];
        s1 = t[1];
        s2 = t[2];
        s3 = t[3];
    }

    /*
     * apply last round and
     * map cipher state to byte array block:
     */

    *(u32 *)(out + 0) =
        (Te2[(s0)&0xff] & 0x000000ffU) ^
        (Te3[(s1 >> 8) & 0xff] & 0x0000ff00U) ^
        (Te0[(s2 >> 16) & 0xff] & 0x00ff0000U) ^
        (Te1[(s3 >> 24)] & 0xff000000U) ^
        rk[0];
    *(u32 *)(out + 4) =
        (Te2[(s1)&0xff] & 0x000000ffU) ^
        (Te3[(s2 >> 8) & 0xff] & 0x0000ff00U) ^
        (Te0[(s3 >> 16) & 0xff] & 0x00ff0000U) ^
        (Te1[(s0 >> 24)] & 0xff000000U) ^
        rk[1];
    *(u32 *)(out + 8) =
        (Te2[(s2)&0xff] & 0x000000ffU) ^
        (Te3[(s3 >> 8) & 0xff] & 0x0000ff00U) ^
        (Te0[(s0 >> 16) & 0xff] & 0x00ff0000U) ^
        (Te1[(s1 >> 24)] & 0xff000000U) ^
        rk[2];
    *(u32 *)(out + 12) =
        (Te2[(s3)&0xff] & 0x000000ffU) ^
        (Te3[(s0 >> 8) & 0xff] & 0x0000ff00U) ^
        (Te0[(s1 >> 16) & 0xff] & 0x00ff0000U) ^
        (Te1[(s2 >> 24)] & 0xff000000U) ^
        rk[3];
}

int main(int argc, char *argv[]) {
    char *in = "plaintext-2019-plaintext-2019-plaintext-2019-plaintext-2019-plaintext-2019-plaintext-2019-plaintext-2019-plaintext-2019";
    char *out = (char *)malloc(100);

    FILE *fp = fopen(argv[1], "r");

    loadInput(fp);

    int **secs = new int *[iters];
    for (int i = 0; i < iters; i++) {
        secs[i] = new int[44];
    }

    generateSecretsForAES(iters, secs);

    rk = (int *)malloc(176); // 44 ints
    volatile int *oldrk = rk;

    double time = 0;
    // printf("startTransaction\n");
    auto start_t = __parsec_roi_begin();

    for (int i = 0; i < iters; i++) {
        for (int j = 0; j < 44; j++)
            rk[j] = secs[i][j];
        AES_encrypt(in, out);
        rk = oldrk;
    }

    auto end_t = __parsec_roi_end();
    auto time_span = end_t - start_t;
    time += time_span;

    printCycles(time, argv[2]);
}
