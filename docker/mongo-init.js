// Runs once on the first start of an empty data volume (/docker-entrypoint-initdb.d/).
// Creates the application user in the admin database, so the server's connection string needs no authSource.
// The user may only read and write the application database.
const appUser = process.env.MONGO_APP_USERNAME;
const appPassword = process.env.MONGO_APP_PASSWORD;

if (!appUser || !appPassword) {
    throw new Error('MONGO_APP_USERNAME and MONGO_APP_PASSWORD must be set');
}

db.getSiblingDB('admin').createUser({
    user: appUser,
    pwd: appPassword,
    roles: [{ role: 'readWrite', db: 'structura_systems' }]
});
