# Maintainer: Dacraezy1 <younes>
pkgname=flint-git
pkgver=0.1.0
pkgrel=1
pkgdesc="Minimal, Linux-native Minecraft Java Edition launcher inspired by the suckless philosophy."
arch=('x86_64')
url="https://github.com/Dacraezy1/Flint"
license=('GPL3')
depends=('qt6-base')
makedepends=('git' 'cmake' 'ninja')
provides=('flint')
conflicts=('flint')
source=("git+https://github.com/Dacraezy1/Flint.git")
md5sums=('SKIP')

build() {
    cmake -B build -S "$srcdir/Flint" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build
}

package() {
    DESTDIR="$pkgdir" cmake --install build
}
