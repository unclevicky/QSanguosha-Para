#include "roomthreadxmode.h"
#include "room.h"
#include "engine.h"
#include "settings.h"
#include "generalselector.h"

#include <QDateTime>

RoomThreadXMode::RoomThreadXMode(Room *room)
    :room(room)
{
    room->getRoomState()->reset();
}

void RoomThreadXMode::run() {
    // initialize the random seed for this thread
    qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));

    QString scheme = Config.value("XMode/RoleChooseX", "Normal").toString();
    assignRoles(scheme);
    room->adjustSeats();

    foreach (ServerPlayer *player, room->m_players) {
        switch (player->getRoleEnum()) {
        case Player::Lord: warm_leader = player; break;
        case Player::Renegade: cool_leader = player; break;
        default:
                break;
        }
    }

    QList<const General *> generals = QList<const General *>();
    /*foreach (QString pack_name, GetConfigFromLuaState(Sanguosha->getLuaState(), "xmode_packages").toStringList()) {
         const Package *pack = Sanguosha->findChild<const Package *>(pack_name);
         if (pack) generals << pack->findChildren<const General *>();
    }*/
    generals << Sanguosha->findChild<const Package *>("standard")->findChildren<const General *>();
    generals << Sanguosha->findChild<const Package *>("wind")->findChildren<const General *>();
    generals << Sanguosha->findChild<const Package *>("fire")->findChildren<const General *>();

    foreach (const General *general, generals) {
        if (general->isTotallyHidden())
            continue;
        general_names << general->objectName();
    }

    general_names.removeOne("huatuo");
    general_names.removeOne("zhangjiao");
    general_names.removeOne("yuji");
    /*foreach (QString name, Config.value("Banlist/XMode").toStringList())
        if (general_names.contains(name)) general_names.removeOne(name);*/

    qShuffle(general_names);

    int index = 0;
    QStringList names;
    foreach (ServerPlayer *p, room->m_players) {
        names.clear();
        for (int i = 0; i < 5; i++) {
            names << general_names.at(index);
            index++;
        }
        startArrange(p, names);
    }
    room->sem->acquire(6);

    QStringList warm_backup, cool_backup;
    foreach (ServerPlayer *player, room->m_players) {
        if (player->getRole().startsWith("r")) {
            player->tag["XModeLeader"] = QVariant::fromValue((PlayerStar)cool_leader);
            cool_backup.append(player->tag["XModeBackup"].toStringList());
        } else {
            player->tag["XModeLeader"] = QVariant::fromValue((PlayerStar)warm_leader);
            warm_backup.append(player->tag["XModeBackup"].toStringList());
        }
        player->tag.remove("XModeBackup");
    }
    startArrange(warm_leader, warm_backup);
    startArrange(cool_leader, cool_backup);

    room->sem->acquire(2);
}

void RoomThreadXMode::startArrange(ServerPlayer *player, const QStringList &to_arrange) {
    if (!player->isOnline()) {
        // @todo: AI
        QStringList arranged = to_arrange.mid(0, 3);
        arrange(player, arranged);
    } else {
        player->invoke("startArrange", to_arrange.join("+"));
    }
}

void RoomThreadXMode::arrange(ServerPlayer *player, const QStringList &arranged) {
    Q_ASSERT(arranged.length() == 3);

    if (!player->hasFlag("XModeGeneralSelected")) {
        QStringList left = arranged.mid(1, 2);
        player->tag["XModeBackup"] = QVariant::fromValue(left);
        player->setGeneralName(arranged.first());
        player->setFlags("XModeGeneralSelected");
    } else {
        player->setFlags("-XModeGeneralSelected");
        QStringList backup = arranged;
        player->tag["XModeBackup"] = QVariant::fromValue(backup);
    }

    room->sem->release();
}

void RoomThreadXMode::assignRoles(const QStringList &roles, const QString &scheme) {
    QStringList all_roles = roles;
    QStringList roleChoices = all_roles;
    roleChoices.removeDuplicates();
    QList<ServerPlayer *> new_players, abstained;
    for (int i = 0; i < 6; i++)
        new_players << NULL;

    foreach (ServerPlayer *player, room->m_players) {
        if (player->isOnline()) {
            QString role = room->askForRole(player, roleChoices, scheme);
            if (role != "abstain") {
                player->setRole(role);
                all_roles.removeOne(role);
                if (!all_roles.contains(role))
                    roleChoices.removeAll(role);

                for (int i = 0; i < 6; i++) {
                    if (roles.at(i) == role && new_players.at(i) == NULL) {
                        new_players[i] = player;
                        break;
                    }
                }

                continue;
            }
        }

        abstained << player;
    }

    if (!abstained.isEmpty()) {
        qShuffle(abstained);

        for (int i = 0; i < 6; i++) {
            if (new_players.at(i) == NULL) {
                new_players[i] = abstained.takeFirst();
                new_players.at(i)->setRole(roles.at(i));
            }
        }
    }

    room->m_players = new_players;
}

// there are 3 scheme
// Normal: choose team1 or team2
// Random: assign role randomly
// AllRoles: select roles directly
void RoomThreadXMode::assignRoles(const QString &scheme) {
    QStringList roles;
    roles << "lord" << "loyalist" << "rebel"
          << "renegade" << "rebel" << "loyalist";

    if (scheme == "Random") {
        qShuffle(roles);
        for (int i = 0; i < roles.length(); i++)
            room->setPlayerProperty(room->m_players.at(i), "role", roles.at(i));
    } else if (scheme == "AllRoles") {
        assignRoles(roles, scheme);
    } else {
        QStringList all_roles;
        all_roles << "leader1" << "guard1" << "guard2"
                  << "leader2" << "guard2" << "guard1";
        assignRoles(all_roles, scheme);

        QMap<QString, QString> map;
        if (qrand() % 2 == 0) {
            map["leader1"] = "lord";
            map["guard1"] = "loyalist";
            map["leader2"] = "renegade";
            map["guard2"] = "rebel";
        } else {
            map["leader1"] = "renegade";
            map["guard1"] = "rebel";
            map["leader2"] = "lord";
            map["guard2"] = "loyalist";
        }

        foreach (ServerPlayer *player, room->m_players)
            player->setRole(map[player->getRole()]);
    }

    bool valid = true;
    QList<ServerPlayer *> players = room->m_players;
	do {
        qShuffle(players);
        valid = true;
        int total = players.length();
        for (int i = 0; i < total; i++) {
            int next = (i + 1) % total;
            int next2 = (next + 1) % total;
            if (players.at(i)->getRole().at(0) == players.at(next)->getRole().at(0)
                && players.at(i)->getRole().at(0) == players.at(next2)->getRole().at(0)) {
                valid = false;
                break;
            }
        }
    } while (!valid);
    room->m_players = players;

    foreach (ServerPlayer *player, room->m_players)
        room->broadcastProperty(player, "role");
}
