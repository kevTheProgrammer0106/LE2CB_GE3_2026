#include "Particle.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <ctime>

// -------------------------------------------------------
// 内部ヘルパー関数
// -------------------------------------------------------

// アップロードヒープ上に指定サイズのバッファリソースを生成して返す
static ResourceObject CreateUploadBuffer(
    ID3D12Device* device,
    size_t sizeInBytes)
{
    ResourceObject resource;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeInBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource.resource));

    assert(SUCCEEDED(hr));

    return resource;
}

// -------------------------------------------------------
// プライベートヘルパー関数
// -------------------------------------------------------

// [min, max] の範囲で一様乱数（float）を返す
float ParticleEmitter::RandomFloat(float min, float max)
{
    return min + (max - min) * (static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
}

// 指定した速さ speed を持つランダムな方向の速度ベクトルを返す
// 球面座標（theta, phi）を使ってランダムな3D方向を生成する
Vector4 ParticleEmitter::RandomVelocity(float speed)
{
    float theta = RandomFloat(0.0f, (float(M_PI))); // 水平角（0〜2π）
    float phi = RandomFloat(0.0f, float(M_PI)); // 仰角（0〜π）
    return { sinf(phi) * cosf(theta) * speed, speed * 2.0f, cosf(phi) * sinf(theta) * speed, 0.0f};
}

// パーティクルを1個生成してプールに追加する
void ParticleEmitter::SpawnParticle()
{
    if (static_cast<int>(particles_.size()) >= MAX_PARTICLES) return;

    float speed = RandomFloat(config.speedMin, config.speedMax);

    // パーティクルごとに独立したランダム発生位置を決定する
    // config.position は基準点として変更せず、オフセットのみ個別に生成する
    Vector4 spawnPos = {
        config.position.x + RandomFloat(-3.0f, 3.0f),
        config.position.y + RandomFloat(-1.5f, 1.5f),
        config.position.z + RandomFloat(-3.0f, 3.0f),
        config.position.w
    };

    Particle p{};
    p.transform.translate = spawnPos;
    p.spawnOrigin = spawnPos;  // 引き寄せ先として各自が記憶しておく
    p.transform.scale = { config.sizeStart, config.sizeStart, config.sizeStart, 1.0f };
    p.transform.rotate = { 0.0f, 0.0f, 0.0f, 0.0f };
    p.color = config.colorStart;
    p.velocity = RandomVelocity(speed);
    p.lifetime = RandomFloat(config.lifetimeMin, config.lifetimeMax);
    p.age = 0.0f;
    p.active = true;

    particles_.push_back(p);
}

// -------------------------------------------------------
// 初期化
// -------------------------------------------------------

void ParticleEmitter::Initialize(ID3D12Device* device)
{
    assert(!initialized_); // 二重初期化を防止
    srand(static_cast<unsigned int>(time(nullptr))); // 乱数シードを時刻で初期化

    // ビルボード用クワッドの共有頂点バッファを生成（6頂点 × 2三角形）
    sharedVertexResource_ = CreateUploadBuffer(device, sizeof(VertexData) * 6);
    const Vector3 normal = { 0.0f, 0.0f, 1.0f };

    // 頂点データを CPU からマップして書き込む
    VertexData* verts = nullptr;
    sharedVertexResource_.Get()->Map(0, nullptr, reinterpret_cast<void**>(&verts));
    verts[0] = { { -0.5f,  0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f }, normal }; // 左上
    verts[1] = { { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f }, normal }; // 左下
    verts[2] = { {  0.5f,  0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f }, normal }; // 右上（三角形1）

    verts[3] = { {  0.5f,  0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f }, normal }; // 右上（三角形2）
    verts[4] = { { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f }, normal }; // 左下
    verts[5] = { {  0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f }, normal }; // 右下
    sharedVertexResource_.Get()->Unmap(0, nullptr);

    // 頂点バッファビューの設定
    vertexBufferView_.BufferLocation = sharedVertexResource_.Get()->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // パーティクルごとの WVP 行列・マテリアルバッファを生成してマップする
    for (int i = 0; i < MAX_PARTICLES; ++i)
    {
        // WVP 行列バッファ：単位行列で初期化
        wvpResources_[i] = CreateUploadBuffer(device, sizeof(TransformationMatrix));
        HRESULT hr = wvpResources_[i].Get()->Map( 0, nullptr, reinterpret_cast< void** >(&wvpData_[i]));
        wvpResources_[i].Get()->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_[i]));
        
        assert(SUCCEEDED(hr));

        wvpData_[i]->WVP = MakeIdentityMatrix();
        wvpData_[i]->World = MakeIdentityMatrix();

        materialResources_[i] = CreateUploadBuffer(device, sizeof(Material));

        hr = materialResources_[i].Get()->Map( 0, nullptr, reinterpret_cast< void** >( &materialData_[i] ));
        assert(SUCCEEDED(hr));

        materialData_[i]->color = { 1.0f, 1.0f, 1.0f, 1.0f };
		materialData_[i]->lightingMode = LightingMode::None;
		materialData_[i]->uvTransform = MakeIdentityMatrix();
    }

    // パーティクルプールの容量を事前確保
    particles_.reserve(MAX_PARTICLES);

    // 炎の色設定：生成時は黄色（高温）→ 消滅時は赤（低温・半透明）
    config.colorStart = {
        255.0f / 255.0f, // R：最大
        232.0f / 255.0f, // G：黄みがかった白
         40.0f / 255.0f, // B：ほぼゼロ
        255.0f / 255.0f  // A：完全不透明
    };

    config.colorEnd = {
        255.0f / 255.0f, // R：最大（赤）
          0.0f / 255.0f, // G：ゼロ
          0.0f / 255.0f, // B：ゼロ
        0.0f / 255.0f  // A：半透明
    };

    initialized_ = true;
}

// -------------------------------------------------------
// シャットダウン
// -------------------------------------------------------

void ParticleEmitter::Shutdown()
{
    if (!initialized_) return; // 未初期化なら何もしない

    // パーティクルごとの GPU リソースをアンマップして解放
    for (int i = 0; i < MAX_PARTICLES; ++i)
    {
        if (wvpResources_[i])
        {
            wvpResources_[i].Get()->Unmap(0, nullptr);
            wvpResources_[i].Reset();
            wvpResources_[i] = nullptr;
            wvpData_[i] = nullptr;
        }
        if (materialResources_[i])
        {
            materialResources_[i].Get()->Unmap(0, nullptr);
            materialResources_[i].Reset();
            materialResources_[i] = nullptr;
            materialData_[i] = nullptr;
        }
    }

    // 共有頂点バッファを解放
    if (sharedVertexResource_)
    {
        sharedVertexResource_.Reset();
    }

    // 状態をリセット
    particles_.clear();
    emitAccumulator_ = 0.0f;
    initialized_ = false;
}

// -------------------------------------------------------
// 手動発生
// -------------------------------------------------------

// 指定数のパーティクルを即時発生させる
void ParticleEmitter::Emit(int count)
{
    if ( !initialized_ )
    {
        return;
    }

    for ( int i = 0; i < count; ++i )
    {
        SpawnParticle();
    }
}

// -------------------------------------------------------
// 更新
// -------------------------------------------------------

void ParticleEmitter::Update(float deltaTime)
{
    assert(deltaTime > 0.0f && deltaTime < 1.0f); // 異常なデルタタイムを検出

    // ループ再生が有効なら、emitRate に基づいて自動発生させる
    if (config.looping && config.emitRate > 0.0f)
    {
        emitAccumulator_ += deltaTime * config.emitRate;
        while (emitAccumulator_ >= 1.0f)
        {
            SpawnParticle();
            emitAccumulator_ -= 1.0f;
        }
    }

    for (auto& p : particles_)
    {
        if (!p.active) continue;

        // 経過時間を加算し、生存時間を超えたら非アクティブにする
        p.age += deltaTime;
        if (p.IsDead()) { p.active = false; continue; }

        // 正規化された経過時間（0.0 = 生成直後、1.0 = 消滅直前）
        float t = p.NormalizedAge();

        // -------------------------------------------------------
        // 炎の動き：序盤に激しく広がり、その後オフセット中心軸へ滑らかに引き寄せる
        // -------------------------------------------------------
        // 【序盤フェーズ】生存率 30% 未満：水平方向に激しく広がる
        if (t < 0.10f)
        {
            // burstT: 0.0（生成直後）→ 1.0（バーストフェーズ終端）に正規化
            float burstT = t / 0.10f;
            // EaseInOut で減衰：開始時は緩やかに広がり始め、終端に向けて滑らかに収まる
            float easedBurst = EaseInOut(1.0f, 0.0f, burstT);   // 1.0 → 0.0
            float burstStrength = easedBurst * 10.0f;

            p.transform.translate.x += (p.velocity.x * 0.5f) * deltaTime * burstStrength;
            p.transform.translate.z += (p.velocity.z * 0.5f) * deltaTime * burstStrength;
        }
        // 【中盤以降フェーズ】生存率 30% 以降：各パーティクル自身の発生位置へ引き寄せる
        // config.position（共有値）ではなく p.spawnOrigin（個別値）を使うことで
        // それぞれが独立した軌跡を描く
        else
        {
            // 各パーティクルが自分の生まれた位置を目標に収束する
            float pullX = p.spawnOrigin.x - p.transform.translate.x;
            float pullZ = p.spawnOrigin.z - p.transform.translate.z;

            // smoothstep を EaseInOut に置き換えてより滑らかな引力立ち上がりにする
            float blend = (t - 0.30f) / 0.70f;                         // 0.0〜1.0 に正規化
            float easedBlend = EaseInOut(0.0f, 1.0f, blend);           // コサインで滑らかに
            float pullStrength = easedBlend * (8.0f + t * 12.0f);      // 後半ほど速く収束

            p.transform.translate.x += (p.velocity.x * 0.05f + pullX * pullStrength) * deltaTime;
            p.transform.translate.z += (p.velocity.z * 0.05f + pullZ * pullStrength) * deltaTime;
            // 水平速度を徐々に減衰させて炎先端の収束を表現
            p.velocity.x *= 0.98f;
            p.velocity.z *= 0.98f;
        }
        p.transform.translate.y += p.velocity.y * deltaTime;

        // サイズを生存時間に応じて線形補間（大 → 小）
        float size = config.sizeStart + (config.sizeEnd - config.sizeStart) * t;
        p.transform.scale = { size, size, size, 1.0f };

        // カラーを生存時間に応じて線形補間（黄 → 赤・半透明）
        p.color.x = config.colorStart.x + (config.colorEnd.x - config.colorStart.x) * t;
        p.color.y = config.colorStart.y + (config.colorEnd.y - config.colorStart.y) * t;
        p.color.z = config.colorStart.z + (config.colorEnd.z - config.colorStart.z) * t;
        p.color.w = config.colorStart.w + (config.colorEnd.w - config.colorStart.w) * t;
    }

    // 非アクティブになったパーティクルをプールから削除する
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
            [](const Particle& p) { return !p.active; }),
        particles_.end());
}

// -------------------------------------------------------
// 描画
// -------------------------------------------------------

void ParticleEmitter::Draw(
    ID3D12GraphicsCommandList* commandList,
    const Matrix4x4& viewMatrix,
    const Matrix4x4& projectionMatrix,
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
    ID3D12PipelineState* const* pipelineStates)
{
    if ( particles_.empty() )
    {
        return;
    }

    // 現在のブレンドモードに対応した PSO を選択
    if ( pipelineStates )
    {
        const int blendModeIndex =
            static_cast< int >( config.blendMode );

        commandList->SetPipelineState(
            pipelineStates[blendModeIndex]);
    }

    // パーティクル共通のクワッド
    commandList->IASetVertexBuffers(
        0,
        1,
        &vertexBufferView_);

    // パーティクルテクスチャ
    commandList->SetGraphicsRootDescriptorTable(
        2,
        textureHandle);

    for ( int i = 0;
        i < static_cast< int >(particles_.size());
        ++i )
    {
        const Particle& p = particles_[i];

        if ( !p.active )
        {
            continue;
        }

        // ワールド行列
        Matrix4x4 world = MakeAffineMatrix(
            p.transform.scale,
            p.transform.rotate,
            p.transform.translate);

        // WVP = World * View * Projection
        Matrix4x4 viewProjection =
            Multiply(viewMatrix, projectionMatrix);

        wvpData_[i]->WVP =
            Multiply(world, viewProjection);

        wvpData_[i]->World =
            world;

        // マテリアル
        materialData_[i]->color =
            p.color;

        materialData_[i]->lightingMode =
            LightingMode::None;

        materialData_[i]->uvTransform =
            MakeIdentityMatrix();

        // Material
        commandList->SetGraphicsRootConstantBufferView(
            0,
            materialResources_[i]
            .Get()
            ->GetGPUVirtualAddress());

    // TransformationMatrix
        commandList->SetGraphicsRootConstantBufferView(
            1,
            wvpResources_[i]
            .Get()
            ->GetGPUVirtualAddress());

        commandList->DrawInstanced(
            6,
            1,
            0,
            0);
    }
}