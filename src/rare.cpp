/****************************************************************************
 *                   ^     +----- |  / ^     ^ |     | +-\                  *
 *                  / \    |      | /  |\   /| |     | |  \                 *
 *                 /   \   +---   |<   | \ / | |     | |  |                 *
 *                /-----\  |      | \  |  v  | |     | |  /                 *
 *               /       \ |      |  \ |     | +-----+ +-/                  *
 ****************************************************************************
 * AFKMud Copyright 1997-2015 by Roger Libiez (Samson),                     *
 * Levi Beckerson (Whir), Michael Ward (Tarl), Erik Wolfe (Dwip),           *
 * Cameron Carroll (Cam), Cyberfox, Karangi, Rathian, Raine,                *
 * Xorith, and Adjani.                                                      *
 * All Rights Reserved.                                                     *
 *                                                                          *
 *                                                                          *
 * External contributions from Remcon, Quixadhal, Zarius, and many others.  *
 *                                                                          *
 * Original SMAUG 1.8b written by Thoric (Derek Snider) with Altrag,        *
 * Blodkai, Haus, Narn, Scryn, Swordbearer, Tricops, Gorog, Rennard,        *
 * Grishnakh, Fireblade, Edmond, Conran, and Nivek.                         *
 *                                                                          *
 * Original MERC 2.1 code by Hatchet, Furey, and Kahn.                      *
 *                                                                          *
 * Original DikuMUD code by: Hans Staerfeldt, Katja Nyboe, Tom Madsen,      *
 * Michael Seifert, and Sebastian Hammer.                                   *
 ****************************************************************************
 *                            Rare Items Module                             *
 ****************************************************************************/

#include <sys/stat.h>
#include <dirent.h>
#include "mud.h"
#include "area.h"
#include "auction.h"
#include "clans.h"
#include "connhist.h"
#include "descriptor.h"
#include "new_auth.h"
#include "objindex.h"
#include "overland.h"
#include "roomindex.h"

#ifdef MULTIPORT
extern bool compilelock;
#endif
extern bool bootlock;

auth_data *get_auth_name( const string & );
void check_pfiles( time_t );
void update_connhistory( descriptor_data *, int );
void show_stateflags( char_data * );
void quotes( char_data * );

void char_leaving( char_data * ch, int howleft )
{
    auth_data *old_auth = nullptr;

    /*
     * new auth 
     */
    old_auth = get_auth_name( ch->name );
    if( old_auth != nullptr )
        if( old_auth->state == AUTH_ONLINE )
            old_auth->state = AUTH_OFFLINE; /* Logging off */

    //ch->pcdata->camp = howleft;

    if( howleft == 0 )  /* Rented at an inn */
    {
        switch ( ch->in_room->area->continent )
        {
            case ACON_ONE:
                //ch->pcdata->one = ch->in_room->vnum;
                break;
            default:
                break;
        }
    }

    /*
     * Get 'em dismounted until we finish mount saving -- Blodkai, 4/97 
     */
    if( ch->position == POS_MOUNTED )
        interpret( ch, "dismount" );

    if( ch->morph )
        interpret( ch, "revert" );

    if( ch->desc )
    {
        if( ch->timer > 24 )
            update_connhistory( ch->desc, CONNTYPE_IDLE );
        else
            update_connhistory( ch->desc, CONNTYPE_QUIT );
    }

    list < obj_data * >::iterator iobj;
    for( iobj = ch->carrying.begin(  ); iobj != ch->carrying.end(  ); )
    {
        obj_data *obj = *iobj;
        ++iobj;
    }
    quotes( ch );
    quitting_char = ch;
    ch->save(  );

    if( sysdata->save_pets )
    {
        list < char_data * >::iterator pet;

        for( pet = ch->pets.begin(  ); pet != ch->pets.end(  ); )
        {
            char_data *cpet = *pet;
            ++pet;

            cpet->extract( true );
        }
    }

    /*
     * Synch clandata up only when clan member quits now. --Shaddai 
     */
    if( ch->pcdata->clan )
        save_clan( ch->pcdata->clan );

    saving_char = nullptr;
    ch->extract( true );

    for( int x = 0; x < MAX_WEAR; ++x )
        for( int y = 0; y < MAX_LAYERS; ++y )
            save_equipment[x][y] = nullptr;
}

CMDF( do_quit )
{
    int level = ch->level;

    if( ch->isnpc(  ) )
    {
        ch->print( "NPCs cannot use the quit command.\r\n" );
        return;
    }

    if( !str_cmp( argument, "auto" ) )
    {
        log_printf_plus( LOG_COMM, level, "%s has idled out.", ch->name );
        char_leaving( ch, 3 );
        return;
    }

    if( !ch->is_immortal(  ) )
    {
        if( ch->in_room->area->flags.test( AFLAG_NOQUIT ) )
        {
            ch->print( "You may not quit in this area, it isn't safe!\r\n" );
            return;
        }

        if( ch->in_room->flags.test( ROOM_NOQUIT ) )
        {
            ch->print( "You may not quit here, it isn't safe!\r\n" );
            return;
        }
    }

    if( ch->position == POS_FIGHTING )
    {
        ch->print( "&RNo way! You are fighting.\r\n" );
        return;
    }

    if( ch->position < POS_STUNNED )
    {
        ch->print( "&[blood]You're not DEAD yet.\r\n" );
        return;
    }

    if( ch->get_timer( TIMER_RECENTFIGHT ) > 0 && !ch->is_immortal(  ) )
    {
        ch->print( "&RYour adrenaline is pumping too hard to quit now!\r\n" );
        return;
    }

    if( auction->item != nullptr && ( ( ch == auction->buyer ) || ( ch == auction->seller ) ) )
    {
        ch->print( "&[auction]Wait until you have bought/sold the item on auction.\r\n" );
        return;
    }

    if( ch->inflight )
    {
        ch->print( "&YSkyships are not equipped with parachutes. Wait until you land.\r\n" );
        return;
    }

    ch->print( "&WYou make a hasty break for the confines of reality...\r\n" );
    act( AT_SAY, "A strange voice says, 'We await your return, $n...'", ch, nullptr, nullptr, TO_CHAR );
    ch->print( "&d\r\n" );
    act( AT_BYE, "$n has left the game.", ch, nullptr, nullptr, TO_ROOM );

    log_printf_plus( LOG_COMM, ch->level, "%s has quit.", ch->name );
    char_leaving( ch, 3 );
}
