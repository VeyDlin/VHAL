#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Crypto/JWT/JWT.h>

#include <array>
#include <span>
#include <string_view>


// Host tests of JWT: a token is created from fixed claims and matches a golden token, and the golden token verifies with its secret
// and yields its claims, and is refused with another secret.


// Golden token produced by the JSON writer: its output must not change.
static constexpr std::string_view goldenToken =
	"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
	"eyJpc3MiOiJ2aGFsIiwic3ViIjoiZGV2aWNlLTEiLCJleHAiOjE3MDAwMDAwMDAsImlhdCI6MTY5OTk5MDAwMCwibmFtZSI6IkFubiJ9."
	"Kkm1X5Q_HZFOO3-qoUlcnWqaZZmnDRa9z-TVTuDerYQ";


void CreateTokenGolden() {
	std::array<char, 512> token{};
	size_t length = 0;

	JWT::Header header;
	JWT::Claims claims;
	claims.iss = "vhal";
	claims.sub = "device-1";
	claims.exp = 1700000000;
	claims.iat = 1699990000;
	claims.AddCustomClaim("name", "Ann");

	VHAL_CHECK(JWT::CreateToken(std::span(token), length, header, claims, "secret"));
	VHAL_CHECK_EQ(std::string_view(token.data(), length), goldenToken);
}


void VerifyAndParseClaims() {
	JWT::Header header;
	JWT::Claims claims;
	std::array<char, 256> headerWork{};
	std::array<char, 512> claimsWork{};
	std::array<char, 64> string1{};
	std::array<char, 64> string2{};
	std::array<char, 64> string3{};
	std::array<char, 64> string4{};

	bool verified = JWT::VerifyToken(
		goldenToken, "secret", header, claims,
		std::span(headerWork), std::span(claimsWork),
		std::span(string1), std::span(string2), std::span(string3), std::span(string4)
	);

	VHAL_CHECK(verified);
	VHAL_CHECK_EQ(claims.iss, "vhal");
	VHAL_CHECK_EQ(claims.sub, "device-1");
	VHAL_CHECK_EQ(claims.exp, 1700000000ULL);
	VHAL_CHECK_EQ(claims.iat, 1699990000ULL);
	VHAL_CHECK_EQ(claims.GetCustomClaim("name"), "Ann");
	VHAL_CHECK(!JWT::VerifyToken(goldenToken, "other"));
}


int main() {
	CreateTokenGolden();
	VerifyAndParseClaims();

	return VhalCheck::Finish("TestJwt");
}
