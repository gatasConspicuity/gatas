docker build -t gatas-build-image:2.3.1 .
#docker create --name gatas-builder-container gatas-image
#docker cp gatas-builder-container:/opt/src/pico/build/gatas.uf2 ./gatas.uf2
#docker rm gatas-build-image:2.3.1
#docker create --name gatas-builder-container gatas-build-image
#docker container start gatas-builder-container
#docker run -it --entrypoint bash gatas-build-image:2.3.1

#DIR=$(pwd)

docker run --rm -e PICO_PLATFORM=rp2350 -e PICO_BOARD=pico2_w -e BUILDER_DIR_NAME=docker_release_rp2350 -v $(pwd):/opt/src --entrypoint /build-entrypoint.sh gatas-build-image:2.3.1
