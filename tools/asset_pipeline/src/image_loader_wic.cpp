#include <fabgl/assets/image_pipeline.h>

#include <wincodec.h>
#include <windows.h>

#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace fabgl::assets {

namespace {

template <typename T> class ComPointer final {
  public:
    ~ComPointer() {
        if (pointer_ != nullptr) {
            pointer_->Release();
        }
    }
    ComPointer() = default;
    ComPointer(const ComPointer&) = delete;
    ComPointer& operator=(const ComPointer&) = delete;

    [[nodiscard]] T* get() const noexcept {
        return pointer_;
    }
    [[nodiscard]] T** put() noexcept {
        return &pointer_;
    }

  private:
    T* pointer_ = nullptr;
};

class ComSession final {
  public:
    ComSession() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComSession() {
        if (result_ == S_OK || result_ == S_FALSE) {
            CoUninitialize();
        }
    }
    [[nodiscard]] HRESULT result() const noexcept {
        return result_;
    }

  private:
    HRESULT result_ = E_FAIL;
};

Result<std::wstring> toWide(const std::string& value) {
    if (value.empty() || value.find('\0') != std::string::npos) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, "image path is empty or invalid"));
    }
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return Result<std::wstring>::failure(
            Error(ErrorCode::InvalidArgument, "image path is not valid UTF-8"));
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), length);
    return Result<std::wstring>::success(std::move(result));
}

Error wicError(const char* operation, HRESULT result, const std::string& path) {
    return Error(ErrorCode::InvalidFormat, std::string("WIC image operation failed: ") + operation)
        .addContext("path", path)
        .addContext("hresult", std::to_string(static_cast<long>(result)));
}

} // namespace

Result<Image> loadImage(const std::string& utf8Path) {
    auto path = toWide(utf8Path);
    if (!path) {
        return Result<Image>::failure(path.error());
    }
    ComSession session;
    const auto initialized = session.result();
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        return Result<Image>::failure(wicError("CoInitializeEx", initialized, utf8Path));
    }

    ComPointer<IWICImagingFactory> factory;
    auto result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(factory.put()));
    if (FAILED(result)) {
        return Result<Image>::failure(wicError("create factory", result, utf8Path));
    }
    ComPointer<IWICBitmapDecoder> decoder;
    result = factory.get()->CreateDecoderFromFilename(path.value().c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnLoad, decoder.put());
    if (FAILED(result)) {
        return Result<Image>::failure(wicError("decode file", result, utf8Path));
    }
    ComPointer<IWICBitmapFrameDecode> frame;
    result = decoder.get()->GetFrame(0U, frame.put());
    if (FAILED(result)) {
        return Result<Image>::failure(wicError("read first frame", result, utf8Path));
    }
    ComPointer<IWICBitmapSource> converted;
    result = WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame.get(), converted.put());
    if (FAILED(result)) {
        return Result<Image>::failure(wicError("convert to RGBA", result, utf8Path));
    }
    UINT width = 0;
    UINT height = 0;
    result = converted.get()->GetSize(&width, &height);
    if (FAILED(result) || width == 0U || height == 0U || width > 8192U || height > 8192U) {
        return Result<Image>::failure(wicError("read dimensions", result, utf8Path));
    }
    const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4U) {
        return Result<Image>::failure(
            Error(ErrorCode::CapacityExceeded, "image dimensions overflow"));
    }
    std::vector<std::uint8_t> rgba(pixelCount * 4U);
    const auto stride = width * 4U;
    result =
        converted.get()->CopyPixels(nullptr, stride, static_cast<UINT>(rgba.size()), rgba.data());
    if (FAILED(result)) {
        return Result<Image>::failure(wicError("copy pixels", result, utf8Path));
    }

    Image image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.pixels.resize(pixelCount);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        image.pixels[index] = {rgba[index * 4U], rgba[index * 4U + 1U], rgba[index * 4U + 2U],
                               rgba[index * 4U + 3U]};
    }
    return Result<Image>::success(std::move(image));
}

} // namespace fabgl::assets
