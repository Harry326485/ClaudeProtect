// Optional asset tool. Requires Node.js and sharp, separate from the C++ build.
const fs = require('node:fs/promises');
const path = require('node:path');
const sharp = require('sharp');

async function main() {
    const assets = path.join(__dirname, '..', 'assets');
    const sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256];
    for (const state of ['stopped', 'running']) {
        const source = await fs.readFile(path.join(assets, `claude-${state}.svg`));
        const frames = [];
        for (const size of sizes) {
            frames.push(await sharp(source, { density: 384 }).resize(size, size).png().toBuffer());
        }
        const directory = Buffer.alloc(6 + 16 * frames.length);
        directory.writeUInt16LE(1, 2);
        directory.writeUInt16LE(frames.length, 4);
        let offset = directory.length;
        for (let index = 0; index < frames.length; index++) {
            const entry = 6 + 16 * index;
            const size = sizes[index] === 256 ? 0 : sizes[index];
            directory[entry] = size;
            directory[entry + 1] = size;
            directory.writeUInt16LE(1, entry + 4);
            directory.writeUInt16LE(32, entry + 6);
            directory.writeUInt32LE(frames[index].length, entry + 8);
            directory.writeUInt32LE(offset, entry + 12);
            offset += frames[index].length;
        }
        await fs.writeFile(path.join(assets, `claude-${state}.ico`), Buffer.concat([directory, ...frames]));
    }
}

main().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
