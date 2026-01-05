# shellcheck disable=all

pkgname=mpd-inhibit
pkgver=0.1
pkgrel=1
pkgdesc="Prevent system sleep while MPD is playing"
arch=('x86_64')
url="https://github.com/shvedes/mpd-inhibit"
license=('Unlicense')
depends=('systemd' 'mpd')
makedepends=('gcc' 'make')
source=(
  "Makefile"
  "mpd-inhibit.c"
  "mpd-inhibit.service"
  "readme.txt"
  "LICENSE"
)
sha256sums=(
    'e565b538df6b0f32136d1325ee51c1b0b5ff7e3458b4baf147dfc0774b9e33ee'
    '2860e752768d05fc945df6e659913e34a6f8e17bf9879ee2a7f18f93c84e8ddb'
    'ad6b48f5679961ad29147341b6365abc80bb3ddccb52c92a469db5d2eebc959f'
    '5f69f7e2e66ea3e1ccd08e2f477921295fac4e157e036ee52a6697ca488224e3'
    '6b0382b16279f26ff69014300541967a356a666eb0b91b422f6862f6b7dad17e'
)

build() {
  make
}

package() {
  make DESTDIR="$pkgdir" install

  install -Dm644 readme.txt \
    "$pkgdir/usr/share/doc/$pkgname/readme.txt"
}

