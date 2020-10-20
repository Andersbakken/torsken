#!/usr/bin/env node

var argv = require("minimist")(process.argv.slice(2));
const fs = require("fs");
const path = require("path");
const child_process = require("child_process");

const Pointer = 0;
const Size = 1;
const Time = 2;
const Addresses = 3;

try {
    if (!fs.lstatSync(argv.dir).isDirectory())
        throw new Error(`${argv.dir} is not a directory`);
} catch (err) {
    console.error(err.message);
    process.exit(1);
}

try {
    if (!fs.lstatSync(argv.exec).isFile())
        throw new Error(`${argv.exec} is not a file`);
} catch (err) {
    console.error(err.message);
    process.exit(1);
}

const lddExec = argv.ldd || "ldd";
const addr2lineExec = argv.addr2line || "addr2line";

const resolved = new Map();
const libs = new Set();

function ldd(path)
{
    // console.log("got ldd", path);
    if (libs.has(path))
        return Promise.resolve();
    libs.add(path);
    return new Promise((resolve, reject) => {
        child_process.exec(`${lddExec} ${argv.exec}`, (error, stdout, stderr) => {
            let promises = [];
            if (error) {
                reject(error);
                return;
            }
            console.log("got stdout", stdout);
            stdout.split("\n").filter(x => { return x.indexOf(" => /") !== -1; }).map(line => {
                const p = line.split(" ")[2];
                // console.log("balls", p);
                return ldd(p);
            });
            Promise.all(promises).then(resolve, reject);
        });
    });
}

function resolveAddress(address)
{
    if (resolved.has(address))
        return resolved.get(address);
    return new Promise((resolve, reject) => {
        child_process.exec(`${addr2lineExec}
    });
}

function process(path)
{
    const chain = Promise.resolve();
    fs.readFileSync(path, "utf8").split("\n").forEach(line => {
        const split = line.split(",");
        const obj = {
            pointer: split[Pointer],
            size: split[Size],
            time: parseInt(split[Time]),
            stack: split.slice(Addresses)
        };
        chain = chain.then(Promise.all(obj.stack.map(resolveAddress))).then(results => {


        });
    });
    return chain;
}


ldd(argv.exec).then(() => {
    let chain = Promise.resolve();
    fs.readdirSync(argv.dir).forEach(entry => {
        chain = chain.then(process.bind(undefined, path.join(argv.dir, entry)));
    });
    return chain;
});


// fs.readdirSync(dir).forEach(entry => {
//     process(path.join(dir, entry));
// });
