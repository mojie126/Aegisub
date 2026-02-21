#ifndef OCTOON_IMAGE_LUT_H_
#define OCTOON_IMAGE_LUT_H_

#include <limits>
#include <sstream>
#include <fstream>

namespace octoon::image {
	namespace detail {
		class exception : public std::exception {
		public:
			[[nodiscard]] const char *what() const noexcept override {
				return m.what();
			}

			const int id;

		protected:
			exception(const int id_, const char *what_arg)
				: id(id_), m(what_arg) {}

			static std::string name(const std::string &ename, const int id) {
				return "[lut.exception." + ename + "." + std::to_string(id) + "] ";
			}

		private:
			std::runtime_error m;
		};

		class parse_error final : public exception {
		public:
			static parse_error create(const int id, const size_t byte_, const std::string &what_arg) {
				const std::string w = name("parse_error", id) + "parse error" + (byte_ != 0 ? (" at " + std::to_string(byte_)) : "") + ": " + what_arg;
				return {id, byte_, w.c_str()};
			}

			const size_t byte;

		private:
			parse_error(const int id_, const size_t byte_, const char *what_arg)
				: exception(id_, what_arg), byte(byte_) {}
		};

		template<typename T>
		struct Vector2 {
			T x, y;

			Vector2() = default;

			Vector2(T xx, T yy) noexcept : x(xx), y(yy) {}

			Vector2 &operator+=(T scale) noexcept {
				x += scale;
				y += scale;
				return *this;
			}

			Vector2 &operator-=(T scale) noexcept {
				x -= scale;
				y -= scale;
				return *this;
			}

			Vector2 &operator*=(T scale) noexcept {
				x *= scale;
				y *= scale;
				return *this;
			}

			Vector2 &operator/=(T scale) noexcept {
				x /= scale;
				y /= scale;
				return *this;
			}

			Vector2 &operator+=(const Vector2 &r) noexcept {
				x += r.x;
				y += r.y;
				return *this;
			}

			Vector2 &operator-=(const Vector2 &r) noexcept {
				x -= r.x;
				y -= r.y;
				return *this;
			}

			Vector2 &operator*=(const Vector2 &r) noexcept {
				x *= r.x;
				y *= r.y;
				return *this;
			}

			Vector2 &operator/=(const Vector2 &r) noexcept {
				x /= r.x;
				y /= r.y;
				return *this;
			}

			friend Vector2 operator+(const Vector2 &l, const Vector2 &r) noexcept { return Vector2(l.x + r.x, l.y + r.y); }

			friend Vector2 operator-(const Vector2 &l, const Vector2 &r) noexcept { return Vector2(l.x - r.x, l.y - r.y); }

			friend Vector2 operator*(const Vector2 &l, const Vector2 &r) noexcept { return Vector2(l.x * r.x, l.y * r.y); }

			friend Vector2 operator/(const Vector2 &l, const Vector2 &r) noexcept { return Vector2(l.x / r.x, l.y / r.y); }

			template<typename S>
			friend Vector2 operator+(const Vector2 &l, S scale) noexcept { return Vector2(l.x + scale, l.y + scale); }

			template<typename S>
			friend Vector2 operator-(const Vector2 &l, S scale) noexcept { return Vector2(l.x - scale, l.y - scale); }

			template<typename S>
			friend Vector2 operator*(const Vector2 &l, S scale) noexcept { return Vector2(l.x * scale, l.y * scale); }

			template<typename S>
			friend Vector2 operator/(const Vector2 &l, S scale) noexcept { return Vector2(l.x / scale, l.y / scale); }

			T operator[](std::uint8_t i) const noexcept {
				assert(i < 2);
				return *(&x + i);
			}

			const T &operator[](std::uint8_t i) noexcept {
				assert(i < 2);
				return *(&x + i);
			}
		};

		template<typename T>
		struct Vector3 {
			T x, y, z;

			Vector3() = default;

			Vector3(T xx, T yy, T zz) noexcept : x(xx), y(yy), z(zz) {}

			Vector3 &operator+=(const Vector3 &r) noexcept {
				x += r.x;
				y += r.y;
				z += r.z;
				return *this;
			}

			Vector3 &operator-=(const Vector3 &r) noexcept {
				x -= r.x;
				y -= r.y;
				z -= r.z;
				return *this;
			}

			Vector3 &operator*=(const Vector3 &r) noexcept {
				x *= r.x;
				y *= r.y;
				z *= r.z;
				return *this;
			}

			Vector3 &operator/=(const Vector3 &r) noexcept {
				x /= r.x;
				y /= r.y;
				z /= r.z;
				return *this;
			}

			template<typename S>
			friend Vector3 operator+(const Vector3 &l, S scale) noexcept { return Vector3(l.x + scale, l.y + scale, l.z + scale); }

			template<typename S>
			friend Vector3 operator-(const Vector3 &l, S scale) noexcept { return Vector3(l.x - scale, l.y - scale, l.z - scale); }

			template<typename S>
			friend Vector3 operator*(const Vector3 &l, S scale) noexcept { return Vector3(l.x * scale, l.y * scale, l.z * scale); }

			template<typename S>
			friend Vector3 operator/(const Vector3 &l, S scale) noexcept { return Vector3(l.x / scale, l.y / scale, l.z / scale); }

			friend Vector3 operator+(const Vector3 &l, const Vector3 &r) noexcept { return Vector3(l.x + r.x, l.y + r.y, l.z + r.z); }

			friend Vector3 operator-(const Vector3 &l, const Vector3 &r) noexcept { return Vector3(l.x - r.x, l.y - r.y, l.z - r.z); }

			friend Vector3 operator*(const Vector3 &l, const Vector3 &r) noexcept { return Vector3(l.x * r.x, l.y * r.y, l.z * r.z); }

			friend Vector3 operator/(const Vector3 &l, const Vector3 &r) noexcept { return Vector3(l.x / r.x, l.y / r.y, l.z / r.z); }

			T operator[](std::uint8_t i) const noexcept {
				assert(i < 3);
				return *(&x + i);
			}

			const T &operator[](std::uint8_t i) noexcept {
				assert(i < 3);
				return *(&x + i);
			}
		};

		template<typename T>
		struct Vector4 {
			T x, y, z, w;

			Vector4() = default;

			Vector4(T xx, T yy, T zz, T ww) noexcept : x(xx), y(yy), z(zz), w(ww) {}

			Vector4 &operator+=(const Vector4 &r) noexcept {
				x += r.x;
				y += r.y;
				z += r.z;
				w += r.w;
				return *this;
			}

			Vector4 &operator-=(const Vector4 &r) noexcept {
				x -= r.x;
				y -= r.y;
				z -= r.z;
				w -= r.w;
				return *this;
			}

			Vector4 &operator*=(const Vector4 &r) noexcept {
				x *= r.x;
				y *= r.y;
				z *= r.z;
				w *= r.w;
				return *this;
			}

			Vector4 &operator/=(const Vector4 &r) noexcept {
				x /= r.x;
				y /= r.y;
				z /= r.z;
				w /= r.w;
				return *this;
			}

			friend Vector4 operator+(const Vector4 &l, const Vector4 &r) noexcept { return Vector4(l.x + r.x, l.y + r.y, l.z + r.z, l.w + r.w); }

			friend Vector4 operator-(const Vector4 &l, const Vector4 &r) noexcept { return Vector4(l.x - r.x, l.y - r.y, l.z - r.z, l.w - r.w); }

			friend Vector4 operator*(const Vector4 &l, const Vector4 &r) noexcept { return Vector4(l.x * r.x, l.y * r.y, l.z * r.z, l.w * r.w); }

			friend Vector4 operator/(const Vector4 &l, const Vector4 &r) noexcept { return Vector4(l.x / r.x, l.y / r.y, l.z / r.z, l.w / r.w); }

			T operator[](std::uint8_t i) const noexcept {
				assert(i < 4);
				return *(&x + i);
			}

			const T &operator[](std::uint8_t i) noexcept {
				assert(i < 4);
				return *(&x + i);
			}
		};

		template<typename T, typename = std::enable_if_t<std::is_integral_v<T> | std::is_unsigned_v<T> | std::is_floating_point_v<T>>>
		class basic_lut {
		public:
			std::string name;

			std::uint32_t width{};
			std::uint32_t height{};
			std::uint8_t channel{};
			std::unique_ptr<T[]> data;

			basic_lut() noexcept = default;

			/*
			* @brief 创建3D lut 查找表
			* @param[in] data 3D lut 查找表的像素信息
			* @param[in] w 图片宽度
			* @param[in] h 图片高度
			* @param[in] c 通道数量, RGB = 3, RGBA = 4
			*/
			basic_lut(std::unique_ptr<T> &&_data, const std::uint32_t w, const std::uint32_t h, const std::uint8_t c) noexcept : width(w), height(h), channel(c), data(std::move(_data)) {}

			/*
			* @brief 创建3D lut 查找表
			* @param[in] s .cube文件的起始内容
			* @param[in] n .cube文件的长度
			* @details 加载cube文件并且生成一张3D lut 查找表
			*/
			basic_lut(const char *s, const std::size_t n) noexcept(false) { this->create(s, n); }

			/*
			* @brief 创建默认的 3D lut 查找表, 用于自定义滤镜效果
			* @param[in] size 查找表的大小，宽度为 size * size， 高度为 size
			* @param[in] channel 通道数量， RGB 为 3， RGBA 为 4
			* @details 创建默认的 3D lut 查找表, 用于自定义滤镜效果
			*/
			explicit basic_lut(const std::uint32_t size, const std::uint8_t channel_ = 3) noexcept { this->create(size, channel_); }

			/*
			* @brief 创建3D lut 查找表
			* @param[in] strean .cube文件的流
			* @details 加载cube流并且生成一张3D lut 查找表
			*/
			explicit basic_lut(std::istream &stream) noexcept(false) { this->create(stream); }

			/*
			* @brief 创建默认的 3D lut 查找表, 用于自定义滤镜效果
			* @param[in] size 查找表的大小，宽度为 size * size， 高度为 size
			* @param[in] channel 通道数量， RGB 为 3， RGBA 为 4
			* @details 创建默认的 3D lut 查找表, 用于自定义滤镜效果
			*/
			void create(std::uint32_t size, const std::uint8_t channel_) noexcept {
				this->width = size * size;
				this->height = size;
				this->channel = channel_;
				this->data = std::make_unique<T[]>(this->width * this->height * this->channel);

				const float inv_size = 1.0f / (static_cast<float>(size) - 1.0f);

				for (std::uint32_t z = 0; z < size; z++) {
					for (std::uint32_t y = 0; y < size; y++) {
						for (std::uint32_t x = 0; x < size; x++) {
							const std::size_t n = (y * width + (z * size + x)) * channel;
							this->data[n + 0] = cast<T>(static_cast<float>(x) * inv_size);
							this->data[n + 1] = cast<T>(static_cast<float>(y) * inv_size);
							this->data[n + 2] = cast<T>(static_cast<float>(z) * inv_size);
						}
					}
				}
			}

			/*
			* @brief 创建3D lut 查找表
			* @param[in] s .cube文件的起始内容
			* @param[in] n .cube文件的长度
			* @details 加载cube文件并且生成一张3D lut 查找表
			*/
			void create(const char *s, const std::size_t n) noexcept(false) {
				this->create(std::istringstream(std::string(s, n)));
			}

			/*
			* @brief 创建3D lut 查找表
			* @param[in] strean .cube文件的流
			* @details 加载cube流并且生成一张3D lut 查找表
			*/
			void create(std::istream &stream) noexcept(false) {
				assert(stream.good());

				std::string line;
				std::uint32_t size = 0;

				std::vector<float> values;

				while (std::getline(stream, line)) {
					if (line.empty())
						continue;

					if (line[0] == '#')
						continue;

					if (line.compare(0, 5, "TITLE") == 0) {
						std::string token;
						std::istringstream sin(line);

						sin >> token >> name;
						continue;
					}

					if (line.compare(0, 10, "DOMAIN_MIN") == 0 || line.compare(0, 10, "DOMAIN_MAX") == 0)
						continue;

					if (line.compare(0, 11, "LUT_3D_SIZE") == 0) {
						std::string token;
						std::istringstream sin(line);

						sin >> token >> size;
						continue;
					}

					// 忽略 DaVinci Resolve 17 的视频范围标签
					// 去除行首尾空白字符后检查
					std::string trimmed_line = line;
					trimmed_line.erase(0, trimmed_line.find_first_not_of(" \t\r\n"));
					trimmed_line.erase(trimmed_line.find_last_not_of(" \t\r\n") + 1);
					if (trimmed_line == "LUT_IN_VIDEO_RANGE" || trimmed_line == "LUT_OUT_VIDEO_RANGE")
						continue;

					float r, g, b;

					std::istringstream sin(line);
					sin >> r >> g >> b;

					values.push_back(r);
					values.push_back(g);
					values.push_back(b);
				}

				if (values.size() == size * size * size * 3) {
					this->width = size * size;
					this->height = size;
					this->channel = 3;
					this->data = std::make_unique<T[]>(this->width * this->height * this->channel);

					for (std::size_t r = 0; r < size; r++) {
						for (std::size_t g = 0; g < size; g++) {
							for (std::size_t b = 0; b < size; b++) {
								// .cube file is b-major: b changes fastest, then g, then r.
								auto src_idx = (r * size * size + g * size + b) * 3;
								// Our 2D layout is z-major: (y * width + (z * size + x))
								auto dst_idx = (g * width + (r * size + b)) * this->channel;
								this->data[dst_idx + 0] = cast<T>(values[src_idx + 0]); // R -> x
								this->data[dst_idx + 1] = cast<T>(values[src_idx + 1]); // G -> y
								this->data[dst_idx + 2] = cast<T>(values[src_idx + 2]); // B -> z
							}
						}
					}
				} else {
					throw parse_error::create(0, 0, "The lut element does not match the size of lut");
				}
			}

			/*
			* @brief 最近邻采样
			* @param[in] u x轴的方向的坐标，范围 0.0 ~ 1.0
			* @param[in] v y轴的方向的坐标，范围 0.0 ~ 1.0
			* @return 返回在归一化纹理坐标(u, v) 处通过最近邻采样获取的像素数据
			* @detail https://en.wikipedia.org/wiki/Nearest-neighbor_interpolation
			*/
			template<typename Elem>
			std::enable_if_t<std::is_floating_point_v<Elem>, Vector3<Elem>> fetch(Elem u, Elem v) noexcept {
				assert(this->data);
				assert(this->channel == 3 || this->channel == 4);
				assert(this->width == this->height * this->height);

				auto x = std::min(std::max(0.0f, u), 1.0f) * this->width;
				auto y = std::min(std::max(0.0f, v), 1.0f) * this->height;
				auto n = (this->width * static_cast<int>(std::round(y)) + static_cast<int>(std::round(x))) * this->channel;

				return Vector3<T>(this->data[n], this->data[n + 1], this->data[n + 2]);
			}

			/*
			* @brief 最近邻采样
			* @param[in] u x轴的方向的绝对像素坐标，范围 0 ~ width
			* @param[in] v y轴的方向的绝对像素坐标，范围 0 ~ height
			* @return 返回在绝对像素纹理坐标(u, v) 处通过最近邻采样像素数据
			* @detail https://en.wikipedia.org/wiki/Nearest-neighbor_interpolation
			*/
			template<typename Elem>
			std::enable_if_t<std::is_unsigned_v<Elem> | std::is_integral_v<Elem>, Vector3<Elem>> fetch(Elem u, Elem v) noexcept {
				assert(this->data);
				assert(this->channel == 3 || this->channel == 4);
				assert(this->width == this->height * this->height);

				auto x = std::min(std::max(0, u), this->width);
				auto y = std::min(std::max(0, v), this->height);
				auto n = (this->width * y + x) * this->channel;

				return Vector3<T>(this->data[n], this->data[n + 1], this->data[n + 2]);
			}

			/*
			* @brief 双线性过滤采样
			* @param[in] u x轴的方向的归一化坐标，范围 0.0 ~ 1.0
			* @param[in] v y轴的方向的归一化坐标，范围 0.0 ~ 1.0
			* @return 返回在归一化纹理坐标(u, v) 处通过双线性采样获取的像素数据
			* @detail https://en.wikipedia.org/wiki/Bilinear_interpolation#Alternative_algorithm
			*/
			template<typename Elem>
			std::enable_if_t<std::is_floating_point_v<Elem>, Vector3<Elem>> lookup(Elem u, Elem v) noexcept {
				assert(u >= 0.0f && u <= 1.0f);
				assert(v >= 0.0f && v <= 1.0f);
				assert(this->data);
				assert(this->channel == 3 || this->channel == 4);
				assert(this->width == this->height * this->height);

				// Scale and clamp to avoid sampling outside the texture
				Elem x = u * (this->width - 1);
				Elem y = v * (this->height - 1);

				Elem x_floor = std::floor(x);
				Elem y_floor = std::floor(y);

				auto x0 = static_cast<std::uint32_t>(x_floor);
				auto y0 = static_cast<std::uint32_t>(y_floor);
				auto x1 = std::min(x0 + 1, this->width - 1);
				auto y1 = std::min(y0 + 1, this->height - 1);

				Elem xw = x - x_floor;
				Elem yw = y - y_floor;

				// bilinear interpolation
				auto n1 = (this->width * y0 + x0) * this->channel;
				auto n2 = (this->width * y0 + x1) * this->channel;
				auto n3 = (this->width * y1 + x0) * this->channel;
				auto n4 = (this->width * y1 + x1) * this->channel;

				auto c00 = Vector3<Elem>(this->data[n1], this->data[n1 + 1], this->data[n1 + 2]);
				auto c10 = Vector3<Elem>(this->data[n2], this->data[n2 + 1], this->data[n2 + 2]);
				auto c01 = Vector3<Elem>(this->data[n3], this->data[n3 + 1], this->data[n3 + 2]);
				auto c11 = Vector3<Elem>(this->data[n4], this->data[n4 + 1], this->data[n4 + 2]);

				auto c0 = lerp(c00, c10, xw);
				auto c1 = lerp(c01, c11, xw);

				return lerp(c0, c1, yw);
			}

			/*
			* @brief 双线性过滤采样
			* @param[in] u x轴的方向的整数范围内未归一化坐标，范围 0 ~ 整数数据类型最大值
			* @param[in] v y轴的方向的整数范围内未归一化坐标，范围 0 ~ 整数数据类型最大值
			* @return 返回在整数范围内未归一化纹理坐标(u, v) 处通过双线性采样获取的像素数据
			* @detail https://en.wikipedia.org/wiki/Bilinear_interpolation#Alternative_algorithm
			*/
			template<typename Elem, typename Float = float>
			std::enable_if_t<std::is_integral_v<Elem> | std::is_unsigned_v<Elem>, Vector3<Elem>> lookup(const Elem u, const Elem v) {
				auto uu = static_cast<Float>(u) / std::numeric_limits<Elem>::max();
				auto vv = static_cast<Float>(v) / std::numeric_limits<Elem>::max();
				auto pixel = lookup(uu, vv);
				return Vector3<Elem>(cast<Elem, T>(pixel[0]), cast<Elem, T>(pixel[1]), cast<Elem, T>(pixel[2]));
			}

			/*
			* @brief 立方体纹理的三线性过滤采样
			* @param[in] u x轴的方向的归一化立方体坐标，范围 0.0 ~ 1.0
			* @param[in] v y轴的方向的归一化立方体坐标，范围 0.0 ~ 1.0
			* @param[in] w z轴的方向的归一化立方体坐标，范围 0.0 ~ 1.0
			* @return 返回在归一化立方体纹理坐标(u, v, w) 处通过三线性采样获取的像素数据
			*/
			template<typename Elem>
			std::enable_if_t<std::is_floating_point_v<Elem>, Vector3<Elem>> lookup(const Elem u, const Elem v, const Elem w) {
				assert(u >= 0.0f && u <= 1.0f);
				assert(v >= 0.0f && v <= 1.0f);
				assert(w >= 0.0f && w <= 1.0f);
				assert(this->data);
				assert(this->channel == 3 || this->channel == 4);
				assert(this->width == this->height * this->height);

				auto size = this->height;
				Elem scaled_r = u * (size - 1);
				Elem scaled_g = v * (size - 1);
				Elem scaled_b = w * (size - 1);

				auto r_floor = std::floor(scaled_r);
				auto g_floor = std::floor(scaled_g);
				auto b_floor = std::floor(scaled_b);

				auto r0 = static_cast<std::uint32_t>(r_floor);
				auto g0 = static_cast<std::uint32_t>(g_floor);
				auto b0 = static_cast<std::uint32_t>(b_floor);

				auto r1 = std::min(r0 + 1, size - 1);
				auto g1 = std::min(g0 + 1, size - 1);
				auto b1 = std::min(b0 + 1, size - 1);

				auto fr = scaled_r - r_floor;
				auto fg = scaled_g - g_floor;
				auto fb = scaled_b - b_floor;

				auto c000 = Vector3<Elem>(data[(g0 * width + b0 * size + r0) * channel + 0], data[(g0 * width + b0 * size + r0) * channel + 1], data[(g0 * width + b0 * size + r0) * channel + 2]);
				auto c100 = Vector3<Elem>(data[(g0 * width + b0 * size + r1) * channel + 0], data[(g0 * width + b0 * size + r1) * channel + 1], data[(g0 * width + b0 * size + r1) * channel + 2]);
				auto c010 = Vector3<Elem>(data[(g1 * width + b0 * size + r0) * channel + 0], data[(g1 * width + b0 * size + r0) * channel + 1], data[(g1 * width + b0 * size + r0) * channel + 2]);
				auto c110 = Vector3<Elem>(data[(g1 * width + b0 * size + r1) * channel + 0], data[(g1 * width + b0 * size + r1) * channel + 1], data[(g1 * width + b0 * size + r1) * channel + 2]);
				auto c001 = Vector3<Elem>(data[(g0 * width + b1 * size + r0) * channel + 0], data[(g0 * width + b1 * size + r0) * channel + 1], data[(g0 * width + b1 * size + r0) * channel + 2]);
				auto c101 = Vector3<Elem>(data[(g0 * width + b1 * size + r1) * channel + 0], data[(g0 * width + b1 * size + r1) * channel + 1], data[(g0 * width + b1 * size + r1) * channel + 2]);
				auto c011 = Vector3<Elem>(data[(g1 * width + b1 * size + r0) * channel + 0], data[(g1 * width + b1 * size + r0) * channel + 1], data[(g1 * width + b1 * size + r0) * channel + 2]);
				auto c111 = Vector3<Elem>(data[(g1 * width + b1 * size + r1) * channel + 0], data[(g1 * width + b1 * size + r1) * channel + 1], data[(g1 * width + b1 * size + r1) * channel + 2]);

				auto c00 = lerp(c000, c100, fr);
				auto c10 = lerp(c010, c110, fr);
				auto c01 = lerp(c001, c101, fr);
				auto c11 = lerp(c011, c111, fr);

				auto c0 = lerp(c00, c10, fg);
				auto c1 = lerp(c01, c11, fg);

				return lerp(c0, c1, fb);
			}

			/*
			* @brief 立方体纹理的三线性过滤采样
			* @param[in] u x轴的方向的整数范围内未归一化立方体坐标，范围 0 ~ 整数数据类型最大值
			* @param[in] v y轴的方向的整数范围内未归一化立方体坐标，范围 0 ~ 整数数据类型最大值
			* @param[in] w z轴的方向的整数范围内未归一化立方体坐标，范围 0 ~ 整数数据类型最大值
			* @return 返回在整数范围内未归一化立方体纹理坐标(u, v, w) 处通过三线性采样获取的像素数据
			*/
			template<typename Elem, typename Float = float>
			std::enable_if_t<std::is_integral_v<Elem> | std::is_unsigned_v<Elem>, Vector3<Elem>> lookup(const Elem u, const Elem v, const Elem w) {
				auto uu = static_cast<Float>(u) / std::numeric_limits<Elem>::max();
				auto vv = static_cast<Float>(v) / std::numeric_limits<Elem>::max();
				auto ww = static_cast<Float>(w) / std::numeric_limits<Elem>::max();
				auto pixel = lookup(uu, vv, ww);
				return Vector3<Elem>(cast<Elem, T>(static_cast<T>(pixel[0])), cast<Elem, T>(static_cast<T>(pixel[1])), cast<Elem, T>(static_cast<T>(pixel[2])));
			}

			/*
			* @brief 立方体纹理的三线性过滤采样
			* @param[in] uvw[0] x轴的方向的归一化立方体坐标，范围 0.0 ~ 1.0
			* @param[in] uvw[1] y轴的方向的归一化立方体坐标，范围 0.0 ~ 1.0
			* @param[in] uvw[2] z轴的方向的归一化立方体坐标，范围 0.0 ~ 1.0
			* @return 返回在归一化立方体纹理坐标(u, v, w) 处通过三线性采样获取的像素数据
			*/
			template<typename Elem>
			std::enable_if_t<std::is_floating_point_v<Elem>, Vector3<Elem>> lookup(const Elem uvw[3]) {
				return lookup(uvw[0], uvw[1], uvw[2]);
			}

			/*
			* @brief 立方体纹理的三线性过滤采样
			* @param[in] uvw[0] x轴的方向的整数范围内未归一化立方体坐标，范围 0 ~ 整数数据类型最大值
			* @param[in] uvw[1] y轴的方向的整数范围内未归一化立方体坐标，范围 0 ~ 整数数据类型最大值
			* @param[in] uvw[2] z轴的方向的整数范围内未归一化立方体坐标，范围 0 ~ 整数数据类型最大值
			* @return 返回在整数范围内未归一化立方体纹理坐标(u, v, w) 处通过三线性采样获取的像素数据
			*/
			template<typename Elem>
			std::enable_if_t<std::is_integral_v<Elem> | std::is_unsigned_v<Elem>, Vector3<Elem>> lookup(const Elem uvw[3]) {
				return lookup(uvw[0], uvw[1], uvw[2]);
			}

			/*
			* @brief 立方体纹理的三线性过滤采样
			* @param[in] uvw  归一化立方体纹理坐标 (u, v, w)，范围 0.0 ~ 1.0
			* @param[out] out 归一化立方体纹理坐标 (u, v, w) 处通过三线性采样获取的像素数据
			*/
			template<typename Elem>
			std::enable_if_t<std::is_floating_point_v<Elem>> lookup(const Elem uvw[3], Elem out[3]) {
				auto v = lookup(uvw);
				out[0] = v[0];
				out[1] = v[1];
				out[2] = v[2];
			}

			/*
			* @brief 立方体纹理的三线性过滤采样
			* @param[in] uvw  整数范围内未归一化立方体纹理坐标 (u, v, w)，范围 0.0 ~ 整数数据类型最大值
			* @param[out] out 整数范围内未归一化立方体纹理坐标 (u, v, w) 处通过三线性采样获取的像素数据
			*/
			template<typename Elem, typename Float = float>
			std::enable_if_t<std::is_integral_v<Elem> | std::is_unsigned_v<Elem>> lookup(const Elem uvw[3], Elem out[3]) {
				auto v = lookup<Elem, Float>(uvw[0], uvw[1], uvw[2]);
				out[0] = v[0];
				out[1] = v[1];
				out[2] = v[2];
			}

			/*
			* @brief 立方体纹理的三线性过滤采样
			* @param[in] uvw  归一化立方体纹理坐标 (u, v, w)，范围 0 ~ 1.0f
			* @param[out] out 归一化立方体纹理坐标 (u, v, w) 处通过三线性采样获取的像素数据
			* @param[in] len 立方体纹理坐标的长度
			* @param[in] channel 立方体纹理坐标的通道数量, RGB = 3, RGBA = 4
			*/
			template<typename Elem>
			std::enable_if_t<std::is_floating_point_v<Elem>> lookup(const Elem *uvw, Elem *out, const std::size_t len, const std::uint8_t input_channel = 3) {
				assert(input_channel == 3 || input_channel == 4);

				for (std::size_t i = 0; i < len; i += input_channel)
					lookup(uvw + i, out + i);
			}

			/*
			* @brief 立方体纹理的三线性过滤采样
			* @param[in] uvw  整数范围内未归一化立方体纹理坐标 (u, v, w)，范围 0 ~ 整数数据类型最大值
			* @param[out] out 整数范围内未归一化立方体纹理坐标 (u, v, w) 处通过三线性采样获取的像素数据
			* @param[in] len 立方体纹理坐标的长度
			* @param[in] channel 立方体纹理坐标的通道数量, RGB = 3, RGBA = 4
			*/
			template<typename Elem, typename Float = float>
			std::enable_if_t<std::is_integral_v<Elem> | std::is_unsigned_v<Elem>> lookup(const Elem *uvw, Elem *out, const std::size_t len, const std::uint8_t input_channel = 3) {
				assert(input_channel == 3 || input_channel == 4);

				for (std::size_t i = 0; i < len; i += input_channel)
					lookup<Elem, Float>(uvw + i, out + i);
			}

			/*
			* @brief 将lut中的数据序列化成.cube的字符串流
			*/
			[[nodiscard]] std::string dump() const noexcept {
				assert(this->data);
				assert(this->channel == 3 || this->channel == 4);
				assert(this->width == this->height * this->height);

				std::ostringstream stream;

				stream << "TITLE " << name << std::endl;
				stream << std::endl;

				stream << "LUT_3D_SIZE " << this->height << std::endl;
				stream << std::endl;

				stream << "DOMAIN_MIN 0.0 0.0 0.0" << std::endl;
				stream << "DOMAIN_MAX 1.0 1.0 1.0" << std::endl;
				stream << std::endl;

				auto size = this->height;
				for (std::size_t r = 0; r < size; r++) {
					for (std::size_t g = 0; g < size; g++) {
						for (std::size_t b = 0; b < size; b++) {
							// .cube file is b-major, our 2D layout is z-major
							auto src_idx = (g * width + (r * size + b)) * this->channel;
							auto val_r = cast<float>(data[src_idx + 0]);
							auto val_g = cast<float>(data[src_idx + 1]);
							auto val_b = cast<float>(data[src_idx + 2]);

							stream << val_r << " " << val_g << " " << val_b << std::endl;
						}
					}
				}

				return stream.str();
			}

			/*
			* @brief 解析一个来至cube文件的流
			*/
			static basic_lut parse(std::istream &stream) noexcept(false) {
				if (stream)
					return basic_lut(stream);
				throw std::runtime_error("bad stream");
			}

			/*
			* @brief 解析一个来至cube的文件
			*/
			static basic_lut parse(const std::string &filepath) noexcept(false) {
				if (auto stream = std::ifstream(filepath))
					return basic_lut(stream);
				throw std::runtime_error("failed to open the file: " + filepath);
			}

			/*
			* @brief 解析一个来至cube的文件
			*/
			static basic_lut parse(const char *filepath) noexcept(false) {
				if (auto stream = std::ifstream(filepath))
					return basic_lut(stream);
				throw std::runtime_error(std::string("failed to open the file: ") + filepath);
			}

			/*
			* @brief 将lut中的数据序列化成.cube的字符串流
			*/
			friend std::ostream &operator<<(std::ostream &os, const basic_lut &lut) noexcept {
				os << lut.dump();
				return os;
			}

		private:
			template<typename Tx, typename Ty>
			static std::enable_if_t<std::is_same_v<Tx, Ty>, Tx> cast(Ty x) noexcept {
				return x;
			}

			/*
			* @brief 将整数归一化成浮点数
			*/
			template<typename Tx, typename Ty, typename = std::enable_if_t<std::is_integral_v<Ty> | std::is_unsigned_v<Ty>, Tx>>
			static std::enable_if_t<std::is_floating_point_v<Tx>, Tx> cast(Ty x) noexcept {
				return static_cast<Tx>(Tx(x) / std::numeric_limits<Ty>::max());
			}

			/*
			* @brief 将浮点数乘算整数最大值
			*/
			template<typename Tx, typename Ty, typename = std::enable_if_t<std::is_floating_point_v<Ty>>>
			static std::enable_if_t<std::is_integral_v<Tx> | std::is_unsigned_v<Tx>, Tx> cast(Ty x) noexcept {
				return static_cast<Tx>(std::min(std::max(x, 0.0f), 1.0f) * std::numeric_limits<Tx>::max());
			}

			/*
			* @brief 取小数部分
			*/
			template<typename Tx>
			static Tx frac(const Tx x) noexcept {
				return x - std::floor(x);
			}

			/*
			* @brief 在t1 和 t2 之间进行线性插值
			* @param[in] t1 初始值
			* @param[in] t2 终止值
			* @param[in] t3 插值的百分比，范围 0.0f ~ 1.0f
			*/
			template<typename Tx, typename Ty>
			static Tx lerp(const Tx t1, const Tx t2, const Ty t3) noexcept {
				if (t3 == 0) return t1; // float-precision
				if (t3 == 1) return t2; // float-precision
				return t1 + (t2 - t1) * t3;
			}
		};
	}

	using lut = detail::basic_lut<std::uint8_t>;

	// 浮点精度LUT类型，用于GPU 3D纹理上传（消除色带）
	using flut = detail::basic_lut<float>;

	// Usage 1
	// auto lut = octoon::image::lut::parse("xxx.cube"); // load the lut from .cube file
	// for (std::size_t i = 0; i < image.size(); i++)
	// {
	//		auto& rgb = image[i];
	// 		auto data = lut.lookup(rgb.r, rgb.g, rgb.b); The (r,g,b) can be extended to support these types of std::uint8_t, std::uint16_t, std::uint32_t, float, double
	// 		rgb.r = data[0];
	// 		rgb.g = data[1];
	// 		rgb.b = data[2];
	// }

	// Usage 2
	// auto lut = octoon::image::lut::parse("xxx.cube"); // load the lut from .cube file
	// lut.lookup(image.data(), image.data(), image.size(), 3); The (r,g,b) can be extended to support these types of std::uint8_t, std::uint16_t, std::uint32_t, float, double

	// Serializable to .cube stream
	// method 1 : std::cout << lut.dump();
	// method 2 : std::cout << lut;

	// Serializable to image
	// auto image = octoon::image::Image(octoon::image::Format::R8G8B8UNorm, lut.width, lut.height);
	// std::memcpy((std::uint8_t*)image.data(), lut.data.get(), lut.width * lut.height * lut.channel);
	// image.save("C:\\Users\\Administrator\\Desktop\\1.png", "png");
}


#endif
