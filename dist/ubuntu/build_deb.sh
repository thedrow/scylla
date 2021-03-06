#!/bin/sh -e

if [ ! -e dist/ubuntu/build_deb.sh ]; then
    echo "run build_deb.sh in top of scylla dir"
    exit 1
fi

if [ -e debian ] || [ -e build/release ]; then
    rm -rf debian build
    mkdir build
fi

VERSION=$(./SCYLLA-VERSION-GEN)
SCYLLA_VERSION=$(cat build/SCYLLA-VERSION-FILE)
SCYLLA_RELEASE=$(cat build/SCYLLA-RELEASE-FILE)
if [ "$SCYLLA_VERSION" = "development" ]; then
	SCYLLA_VERSION=0development
fi
echo $VERSION > version
./scripts/git-archive-all --extra version --force-submodules --prefix scylla-server ../scylla-server_$SCYLLA_VERSION-$SCYLLA_RELEASE.orig.tar.gz 

cp -a dist/ubuntu/debian debian
cp dist/common/sysconfig/scylla-server debian/scylla-server.default
cp dist/ubuntu/changelog.in debian/changelog
sed -i -e "s/@@VERSION@@/$SCYLLA_VERSION/g" debian/changelog
sed -i -e "s/@@RELEASE@@/$SCYLLA_RELEASE/g" debian/changelog

sudo apt-get -y update

./dist/ubuntu/dep/build_dependency.sh

sudo apt-get -y install libyaml-cpp-dev liblz4-dev libsnappy-dev libcrypto++-dev libboost1.55-dev libjsoncpp-dev libaio-dev ragel ninja-build git libyaml-cpp0.5 liblz4-1 libsnappy1 libcrypto++9 libboost-program-options1.55.0 libboost-program-options1.55-dev libboost-system1.55.0 libboost-system1.55-dev libboost-thread1.55.0 libboost-thread1.55-dev libboost-test1.55.0 libboost-test1.55-dev libjsoncpp0 libaio1 hugepages software-properties-common libboost-filesystem1.55-dev libboost-filesystem1.55.0
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get -y update
sudo apt-get -y install g++-5

debuild -r fakeroot -us -uc
