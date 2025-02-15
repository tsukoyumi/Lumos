#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragPosition;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in vec3 fragTangent;

struct Light
{
	vec4 colour;
	vec4 position;
	vec4 direction;
	float intensity;
	float radius;
	float type;
	float angle;
};

layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 1) uniform sampler2D u_MetallicMap;
layout(set = 1, binding = 2) uniform sampler2D u_RoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D u_NormalMap;
layout(set = 1, binding = 4) uniform sampler2D u_AOMap;
layout(set = 1, binding = 5) uniform sampler2D u_EmissiveMap;

layout(set = 1,binding = 6) uniform UniformMaterialData
{
	vec4  albedoColour;
	vec4  RoughnessColour;
	vec4  metallicColour;
	vec4  emissiveColour;
	float usingAlbedoMap;
	float usingMetallicMap;
	float usingRoughnessMap;
	float usingNormalMap;
	float usingAOMap;
	float usingEmissiveMap;
	float workflow;
	float padding;
} materialProperties;

#define PI 3.1415926535897932384626433832795
#define GAMMA 2.2
#define MAX_LIGHTS 32
#define MAX_SHADOWMAPS 4

const int NumPCFSamples = 16;
const bool fadeCascades = false;
const float Epsilon = 0.00001;
float ShadowFade = 1.0;

layout(set = 2, binding = 0) uniform sampler2D uPreintegratedFG;
layout(set = 2, binding = 1) uniform samplerCube uEnvironmentMap;
layout(set = 2, binding = 2) uniform samplerCube uIrradianceMap;
layout(set = 2, binding = 3) uniform sampler2DArray uShadowMap;

layout(set = 2, binding = 4) uniform UniformBufferLight
{
	Light lights[MAX_LIGHTS];
	mat4 uShadowTransform[MAX_SHADOWMAPS];
	mat4 viewMatrix;
	mat4 lightView;
	mat4 biasMat;
	vec4 cameraPosition;
	vec4 uSplitDepths[MAX_SHADOWMAPS];
	float lightSize;
	float maxShadowDistance;
	float shadowFade;
	float cascadeTransitionFade;
	int lightCount;
	int shadowCount;
	int mode;
	int cubemapMipLevels;
	float initialBias;
} ubo;

layout(location = 0) out vec4 outColor;

const float PBR_WORKFLOW_SEPARATE_TEXTURES = 0.0f;
const float PBR_WORKFLOW_METALLIC_ROUGHNESS = 1.0f;
const float PBR_WORKFLOW_SPECULAR_GLOSINESS = 2.0f;

#define PI 3.1415926535897932384626433832795
#define GAMMA 2.2

struct Material
{
	vec4 Albedo;
	vec3 Metallic;
	float Roughness;
	vec3 Emissive;
	vec3 Normal;
	float AO;
	vec3 View;
	float NDotV;
};

vec4 GammaCorrectTexture(vec4 samp)
{
	return samp;
	return vec4(pow(samp.rgb, vec3(GAMMA)), samp.a);
}

vec3 GammaCorrectTextureRGB(vec4 samp)
{
	return samp.xyz;
	return vec3(pow(samp.rgb, vec3(GAMMA)));
}

vec4 GetAlbedo()
{
	return (1.0 - materialProperties.usingAlbedoMap) * materialProperties.albedoColour + materialProperties.usingAlbedoMap * GammaCorrectTexture(texture(u_AlbedoMap, fragTexCoord));
}

vec3 GetMetallic()
{
	return (1.0 - materialProperties.usingMetallicMap) * materialProperties.metallicColour.rgb + materialProperties.usingMetallicMap * GammaCorrectTextureRGB(texture(u_MetallicMap, fragTexCoord)).rgb;
}

float GetRoughness()
{
	return (1.0 - materialProperties.usingRoughnessMap) *  materialProperties.RoughnessColour.r + materialProperties.usingRoughnessMap * GammaCorrectTextureRGB(texture(u_RoughnessMap, fragTexCoord)).r;
}

float GetAO()
{
	return (1.0 - materialProperties.usingAOMap) + materialProperties.usingAOMap * GammaCorrectTextureRGB(texture(u_AOMap, fragTexCoord)).r;
}

vec3 GetEmissive()
{
	return (1.0 - materialProperties.usingEmissiveMap) * materialProperties.emissiveColour.rgb + materialProperties.usingEmissiveMap * GammaCorrectTextureRGB(texture(u_EmissiveMap, fragTexCoord));
}

vec3 GetNormalFromMap()
{
	if (materialProperties.usingNormalMap < 0.1)
		return normalize(fragNormal);
	
	vec3 tangentNormal = texture(u_NormalMap, fragTexCoord).xyz * 2.0 - 1.0;
	
	vec3 Q1 = dFdx(fragPosition.xyz);
	vec3 Q2 = dFdy(fragPosition.xyz);
	vec2 st1 = dFdx(fragTexCoord);
	vec2 st2 = dFdy(fragTexCoord);
	
	vec3 N = normalize(fragNormal);
	vec3 T = normalize(Q1*st2.t - Q2*st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);
	
	return normalize(TBN * tangentNormal);
}


const mat4 biasMat = mat4(
						  0.5, 0.0, 0.0, 0.5,
						  0.0, 0.5, 0.0, 0.5,
						  0.0, 0.0, 1.0, 0.0,
						  0.0, 0.0, 0.0, 1.0
						  );

const vec2 PoissonDistribution16[16] = vec2[](
											  vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
											  vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464), vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
											  vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
											  vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
											  );


const vec2 PoissonDistribution[64] = vec2[](
											vec2(-0.884081, 0.124488), vec2(-0.714377, 0.027940), vec2(-0.747945, 0.227922), vec2(-0.939609, 0.243634),
											vec2(-0.985465, 0.045534),vec2(-0.861367, -0.136222),vec2(-0.881934, 0.396908),vec2(-0.466938, 0.014526),
											vec2(-0.558207, 0.212662),vec2(-0.578447, -0.095822),vec2(-0.740266, -0.095631),vec2(-0.751681, 0.472604),
											vec2(-0.553147, -0.243177),vec2(-0.674762, -0.330730),vec2(-0.402765, -0.122087),vec2(-0.319776, -0.312166),
											vec2(-0.413923, -0.439757),vec2(-0.979153, -0.201245),vec2(-0.865579, -0.288695),vec2(-0.243704, -0.186378),
											vec2(-0.294920, -0.055748),vec2(-0.604452, -0.544251),vec2(-0.418056, -0.587679),vec2(-0.549156, -0.415877),
											vec2(-0.238080, -0.611761),vec2(-0.267004, -0.459702),vec2(-0.100006, -0.229116),vec2(-0.101928, -0.380382),
											vec2(-0.681467, -0.700773),vec2(-0.763488, -0.543386),vec2(-0.549030, -0.750749),vec2(-0.809045, -0.408738),
											vec2(-0.388134, -0.773448),vec2(-0.429392, -0.894892),vec2(-0.131597, 0.065058),vec2(-0.275002, 0.102922),
											vec2(-0.106117, -0.068327),vec2(-0.294586, -0.891515),vec2(-0.629418, 0.379387),vec2(-0.407257, 0.339748),
											vec2(0.071650, -0.384284),vec2(0.022018, -0.263793),vec2(0.003879, -0.136073),vec2(-0.137533, -0.767844),
											vec2(-0.050874, -0.906068),vec2(0.114133, -0.070053),vec2(0.163314, -0.217231),vec2(-0.100262, -0.587992),
											vec2(-0.004942, 0.125368),vec2(0.035302, -0.619310),vec2(0.195646, -0.459022),vec2(0.303969, -0.346362),
											vec2(-0.678118, 0.685099),vec2(-0.628418, 0.507978),vec2(-0.508473, 0.458753),vec2(0.032134, -0.782030),
											vec2(0.122595, 0.280353),vec2(-0.043643, 0.312119),vec2(0.132993, 0.085170),vec2(-0.192106, 0.285848),
											vec2(0.183621, -0.713242),vec2(0.265220, -0.596716),vec2(-0.009628, -0.483058),vec2(-0.018516, 0.435703)
											);


vec2 SamplePoisson(int index)
{
	return PoissonDistribution[index % 64];
}

vec2 SamplePoisson16(int index)
{
	return PoissonDistribution16[index % 16];
}

float PHI = 1.61803398874989484820459;  // Φ = Golden Ratio   

float GoldNoise(vec2 xy, float seed)
{
	return fract(tan(distance(xy*PHI, xy)*seed)*xy.x);
}

float rand(vec2 co)
{
    float a = 12.9898;
    float b = 78.233;
    float c = 43758.5453;
    float dt= dot(co.xy ,vec2(a,b));
    float sn= mod(dt,3.14);
    return fract(sin(sn) * c);
}

float Random(vec4 seed4)
{
	float dot_product = dot(seed4, vec4(12.9898,78.233,45.164,94.673));
    return fract(sin(dot_product) * 43758.5453);
}

float TextureProj(vec4 shadowCoord, vec2 offset, int cascadeIndex, float bias)
{
	float shadow = 1.0;
	float ambient = 0.0;
	
	if ( shadowCoord.z > -1.0 && shadowCoord.z < 1.0 && shadowCoord.w > 0)
	{
		float dist = texture(uShadowMap, vec3(shadowCoord.st + offset, cascadeIndex)).r;
		if (dist < (shadowCoord.z - bias))
		{
			shadow = ambient;//dist;
		}
	}
	return shadow;
	
}

float PCFShadow(vec4 sc, int cascadeIndex, float bias, vec3 wsPos)
{
	ivec2 texDim = textureSize(uShadowMap, 0).xy;
	float scale = 0.75;
	
	vec2 dx = scale * 1.0 / texDim;
	
	float shadowFactor = 0.0;
	int count = 0;
	float range = 1.0;
	
	for (float x = -range; x <= range; x += 1.0) 
	{
		for (float y = -range; y <= range; y += 1.0) 
		{
			shadowFactor += TextureProj(sc, vec2(dx.x * x, dx.y * y), cascadeIndex, bias);
			count++;
		}
	}
	return shadowFactor / count;
}

float PoissonShadow(vec4 sc, int cascadeIndex, float bias, vec3 wsPos)
{
	ivec2 texDim = textureSize(uShadowMap, 0).xy;
	float scale = 0.8;
	
	vec2 dx = scale * 1.0 / texDim;
	
	float shadowFactor = 1.0;
	int count = 0;
	
	for(int i = 0; i < 8; i ++)
	{
		int index = i;// int(16.0*Random(floor(wsPos*1000.0), count))%16;
		shadowFactor -= 0.1 * (1.0 - TextureProj(sc, dx * PoissonDistribution16[index], cascadeIndex, bias));
		count++;
	}
	return shadowFactor;
}

vec2 SearchRegionRadiusUV(float zWorld)
{
	float light_zNear = 0.0; 
	vec2 lightRadiusUV = vec2(0.05);
    return lightRadiusUV * (zWorld - light_zNear) / zWorld;
}

float SearchWidth(float uvLightSize, float receiverDistance, vec3 cameraPos)
{
	const float NEAR = 0.1;
	return uvLightSize * (receiverDistance - NEAR) / cameraPos.z;
}

// PCF + Poisson + RandomSample model method
float PoissonDotShadow(vec4 sc, int cascadeIndex, float bias, vec3 wsPos, float uvRadius)
{	
	float shadowMapDepth = 0.0;
	ivec2 texDim = textureSize(uShadowMap, 0).xy;
	
    for (int i = 0; i < NumPCFSamples; i++)
	{
		int index = int(float(NumPCFSamples)*GoldNoise(wsPos.xy, wsPos.z + i))%NumPCFSamples;
		vec2 pd = (2.0 / texDim) * PoissonDistribution[index];
		float z = texture(uShadowMap, vec3(sc.xy + pd, cascadeIndex)).r;
		shadowMapDepth += (z < (sc.z - bias)) ? 1 : 0;
	}
	
	return shadowMapDepth / float(NumPCFSamples);
}

float GetShadowBias(vec3 lightDirection, vec3 normal, int shadowIndex)
{
	float minBias = ubo.initialBias;
	float bias = max(minBias * (1.0 - dot(normal, lightDirection)), minBias);
	return bias;
}

float PCFShadowDirectionalLight(sampler2DArray shadowMap, vec4 shadowCoords, float uvRadius, vec3 lightDirection, vec3 normal, vec3 wsPos, int cascadeIndex)
{
	float bias = GetShadowBias(lightDirection, normal, cascadeIndex);
	float sum = 0;
	
	for (int i = 0; i < NumPCFSamples; i++)
	{
		//int index = int(16.0f*Random(vec4(wsPos, i)))%16;
		//int index = int(float(NumPCFSamples)*GoldNoise(wsPos.xy, i * wsPos.z))%NumPCFSamples;
		int index = int(float(NumPCFSamples)*Random(vec4(wsPos.xyz, 1)))%NumPCFSamples;
		
		float z = texture(shadowMap, vec3(shadowCoords.xy + (SamplePoisson(index) * uvRadius), cascadeIndex)).r;
		sum += step(shadowCoords.z - bias, z);
		//sum += step(shadowCoords.z - bias, z);
	}
	
	return sum / NumPCFSamples;
}

int CalculateCascadeIndex(vec3 wsPos)
{
	int cascadeIndex = 0;
	vec4 viewPos = ubo.viewMatrix * vec4(wsPos, 1.0);
	
	for(int i = 0; i < ubo.shadowCount - 1; ++i)
	{
		if(viewPos.z < ubo.uSplitDepths[i].x)
		{
			cascadeIndex = i + 1;
		}
	}
	
	return cascadeIndex;
}

float CalculateShadow(vec3 wsPos, int cascadeIndex, vec3 lightDirection, vec3 normal)
{
	vec4 shadowCoord = ubo.biasMat * ubo.uShadowTransform[cascadeIndex] * vec4(wsPos, 1.0);
	shadowCoord = shadowCoord * ( 1.0 / shadowCoord.w);
	float NEAR = 0.01;
	float uvRadius =  ubo.lightSize * NEAR / shadowCoord.z;
	uvRadius = min(uvRadius, 0.002f);
	vec4 viewPos = vec4(wsPos, 1.0) * ubo.viewMatrix;
	
	float shadowAmount = 1.0;
	shadowCoord = ubo.biasMat * ubo.uShadowTransform[cascadeIndex] * vec4(wsPos, 1.0);
	shadowAmount = PCFShadowDirectionalLight(uShadowMap, shadowCoord, uvRadius, lightDirection, normal, wsPos, cascadeIndex);
	
	return 1.0 - ((1.0 - shadowAmount) * ShadowFade);
}

// Constant normal incidence Fresnel factor for all dielectrics.
const vec3 Fdielectric = vec3(0.04);

// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2
float ndfGGX(float cosLh, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSq = alpha * alpha;
	
	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

// Single term for separable Schlick-GGX below.
float gaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method.
float gaSchlickGGX(float cosLi, float NdotV, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
	return gaSchlickG1(cosLi, k) * gaSchlickG1(NdotV, k);
}

// Shlick's approximation of the Fresnel factor.
vec3 fresnelSchlick(vec3 F0, float cosTheta)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(vec3 F0, float cosTheta, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 Lighting(vec3 F0, vec3 wsPos, Material material)
{
	vec3 result = vec3(0.0);
	
	for(int i = 0; i < ubo.lightCount; i++)
	{
		Light light = ubo.lights[i];
		float value = 0.0;
		
		if(light.type == 2.0)
		{
		    // Vector to light
			vec3 L = light.position.xyz - wsPos;
			// Distance from light to fragment position
			float dist = length(L);
			
			// Light to fragment
			L = normalize(L);
			
			// Attenuation
			float atten = light.radius / (pow(dist, 2.0) + 1.0);
			
			value = atten;
			
			light.direction = vec4(L,1.0);
		}
		else if (light.type == 1.0)
		{
			vec3 L = light.position.xyz - wsPos;
			float cutoffAngle   = 1.0f - light.angle;      
			float dist          = length(L);
			L = normalize(L);
			float theta         = dot(L.xyz, light.direction.xyz);
			float epsilon       = cutoffAngle - cutoffAngle * 0.9f;
			float attenuation 	= ((theta - cutoffAngle) / epsilon); // atteunate when approaching the outer cone
			attenuation         *= light.radius / (pow(dist, 2.0) + 1.0);//saturate(1.0f - dist / light.range);
			//float intensity 	= attenuation * attenuation;
			
			
			// Erase light if there is no need to compute it
			//intensity *= step(theta, cutoffAngle);
			
			value = clamp(attenuation, 0.0, 1.0);
		}
		else
		{
			int cascadeIndex = CalculateCascadeIndex(wsPos);
			value = CalculateShadow(wsPos,cascadeIndex, light.direction.xyz, material.Normal);
		}
		
		vec3 Li = light.direction.xyz;
		vec3 Lradiance = light.colour.xyz * light.intensity;
		vec3 Lh = normalize(Li + material.View);
		
		// Calculate angles between surface normal and various light vectors.
		float cosLi = max(0.0, dot(material.Normal, Li));
		float cosLh = max(0.0, dot(material.Normal, Lh));
		
		//vec3 F = fresnelSchlick(F0, max(0.0, dot(Lh, material.View)));
		vec3 F = fresnelSchlickRoughness(F0, max(0.0, dot(Lh,  material.View)), material.Roughness);
		
		float D = ndfGGX(cosLh, material.Roughness);
		float G = gaSchlickGGX(cosLi, material.NDotV, material.Roughness);
		
		vec3 kd = (1.0 - F) * (1.0 - material.Metallic.x);
		vec3 diffuseBRDF = kd * material.Albedo.xyz;
		
		// Cook-Torrance
		vec3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * material.NDotV);
		
		result += (diffuseBRDF + specularBRDF) * Lradiance * cosLi * value * material.AO;
	}
	return result;
}

vec3 RadianceIBLIntegration(float NdotV, float roughness, vec3 metallic)
{
	vec2 preintegratedFG = texture(uPreintegratedFG, vec2(roughness, 1.0 - NdotV)).rg;
	return metallic * preintegratedFG.r + preintegratedFG.g;
}

vec3 IBL(vec3 F0, vec3 Lr, Material material)
{
	vec3 irradiance = texture(uIrradianceMap, material.Normal).rgb;
	vec3 F = fresnelSchlickRoughness(F0, material.NDotV, material.Roughness);
	vec3 kd = (1.0 - F) * (1.0 - material.Metallic.x);
	vec3 diffuseIBL = material.Albedo.xyz * irradiance;
	
	int u_EnvRadianceTexLevels = ubo.cubemapMipLevels;// textureQueryLevels(uPreintegratedFG);	
	vec3 specularIrradiance = textureLod(uEnvironmentMap, Lr, material.Roughness * u_EnvRadianceTexLevels).rgb;
	
	vec2 specularBRDF = texture(uPreintegratedFG, vec2(material.NDotV, 1.0 - material.Roughness.x)).rg;
	vec3 specularIBL = specularIrradiance * (F0 * specularBRDF.x + specularBRDF.y);
	
	return kd * diffuseIBL + specularIBL;
}

vec3 FinalGamma(vec3 colour)
{
	return pow(colour, vec3(1.0 / GAMMA));
}

vec3 GammaCorrectTextureRGB(vec3 texCol)
{
	return vec3(pow(texCol.rgb, vec3(GAMMA)));
}

float Attentuate( vec3 lightData, float dist )
{
	float att =  1.0 / ( lightData.x + lightData.y*dist + lightData.z*dist*dist );
	float damping = 1.0;// - (dist/lightData.w);
	return max(att * damping, 0.0);
}

void main() 
{
	vec4 texColour = GetAlbedo();
	if(texColour.w < 0.4)
		discard;
	
	float metallic = 0.0;
	float roughness = 0.0;
	
	if(materialProperties.workflow == PBR_WORKFLOW_SEPARATE_TEXTURES)
	{
		metallic  = GetMetallic().x;
		roughness = GetRoughness();
	}
	else if( materialProperties.workflow == PBR_WORKFLOW_METALLIC_ROUGHNESS)
	{
		vec3 tex = GammaCorrectTextureRGB(texture(u_MetallicMap, fragTexCoord));
		metallic = tex.b;
		roughness = tex.g;
	}
	else if( materialProperties.workflow == PBR_WORKFLOW_SPECULAR_GLOSINESS)
	{
		//TODO
		vec3 tex = GammaCorrectTextureRGB(texture(u_MetallicMap, fragTexCoord));
		metallic = tex.b;
		roughness = tex.g;
	}
	
	Material material;
    material.Albedo    = texColour;
    material.Metallic  = vec3(metallic);
    material.Roughness = roughness;
    material.Normal    = normalize(GetNormalFromMap());
	material.AO		= GetAO();
	material.Emissive  = GetEmissive();
	
	vec3 wsPos = fragPosition.xyz;
	material.View 	 = normalize(ubo.cameraPosition.xyz - wsPos);
	material.NDotV     = max(dot(material.Normal, material.View), 0.0);

    float shadowDistance = ubo.maxShadowDistance;
	float transitionDistance = ubo.shadowFade;
	
	vec4 viewPos = ubo.viewMatrix * vec4(wsPos, 1.0);
	
	float distance = length(viewPos);
	ShadowFade = distance - (shadowDistance - transitionDistance);
	ShadowFade /= transitionDistance;
	ShadowFade = clamp(1.0 - ShadowFade, 0.0, 1.0);
	
	vec3 Lr = 2.0 * material.NDotV * material.Normal - material.View;
	// Fresnel reflectance, metals use albedo
	vec3 F0 = mix(Fdielectric, material.Albedo.xyz, material.Metallic.x);
	
	 vec3 lightContribution = Lighting(F0, wsPos, material);
	vec3 iblContribution = IBL(F0, Lr, material) * 2.0;
	
	vec3 finalColour = lightContribution + iblContribution + material.Emissive;
	outColor = vec4(finalColour, 1.0);
	
	if(ubo.mode > 0)
	{
		switch(ubo.mode)
		{
			case 1:
			outColor = material.Albedo;
			break;
			case 2:
			outColor = vec4(material.Metallic, 1.0);
			break;
			case 3:
			outColor = vec4(material.Roughness, material.Roughness, material.Roughness,1.0);
			break;
			case 4:
			outColor = vec4(material.AO, material.AO, material.AO, 1.0);
			break;
			case 5:
			outColor = vec4(material.Emissive, 1.0);
			break;
			case 6:
			outColor = vec4(material.Normal,1.0);
			break;
            case 7:
			int cascadeIndex = CalculateCascadeIndex(wsPos);
			switch(cascadeIndex)
			{
				case 0 : outColor = outColor * vec4(0.8,0.2,0.2,1.0); break;
				case 1 : outColor = outColor * vec4(0.2,0.8,0.2,1.0); break;
				case 2 : outColor = outColor * vec4(0.2,0.2,0.8,1.0); break;
				case 3 : outColor = outColor * vec4(0.8,0.8,0.2,1.0); break;
			}
			break;
		}
	}
}